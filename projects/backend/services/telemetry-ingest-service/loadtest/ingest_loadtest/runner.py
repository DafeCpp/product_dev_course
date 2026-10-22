"""Async workers that drive REST and WebSocket ingest."""
from __future__ import annotations

import asyncio
import json
import math
import time
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from itertools import count
from typing import Any

import aiohttp

from ingest_loadtest.scenarios import Scenario
from ingest_loadtest.stats import Recorder, Report, build_report


@dataclass(frozen=True)
class SensorCredential:
    sensor_id: str
    token: str


_TICK = timedelta(microseconds=1)


class ReadingFactory:
    """Builds batches stamped with the send time, strictly increasing per sensor.

    Each batch starts at the current wall clock (or 1 µs after the previous
    reading, whichever is later), so timestamps follow the run and the
    sensor's heartbeat stays fresh. Unique timestamps keep the dedup index
    from dropping readings, so ``accepted`` counts match what was stored.
    """

    def __init__(self, clock: Callable[[], datetime] | None = None) -> None:
        self._clock = clock or (lambda: datetime.now(timezone.utc))
        self._last: datetime | None = None
        self._seq = count()

    def batch(self, size: int) -> list[dict[str, Any]]:
        start = self._clock()
        if self._last is not None and start <= self._last:
            start = self._last + _TICK
        readings = []
        for i in range(size):
            ts = start + i * _TICK
            readings.append(
                {
                    "timestamp": ts.isoformat(timespec="microseconds").replace("+00:00", "Z"),
                    "raw_value": float(next(self._seq) % 1000),
                    "meta": {"signal": "loadtest"},
                }
            )
        if size:
            self._last = start + (size - 1) * _TICK
        return readings


Batch = list[dict[str, Any]]
# Sends a group of batches planned for the same instant (one batch outside a burst).
SendFn = Callable[[list[Batch]], Awaitable[None]]


def _accepted_count(reply: Any, sent: int) -> int | None:
    """Readings the service reports as stored (dedup can drop some), or ``None`` if invalid.

    The ingest contract always returns ``accepted`` within ``0..sent``; any
    other success is counted as an error rather than credited to throughput.
    """
    if isinstance(reply, dict):
        value = reply.get("accepted")
        if isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= sent:
            return value
    return None


def _record_ws_reply(recorder: Recorder, reply: Any, latency_ms: float, sent: int) -> None:
    """Classify one WS reply; anything that is not a JSON object is a malformed ack."""
    if not isinstance(reply, dict):
        recorder.error("ws_ack_malformed")
    elif reply.get("status") == "accepted":
        stored = _accepted_count(reply, sent)
        if stored is None:
            recorder.error("ws_ack_malformed")
        else:
            recorder.accepted(latency_ms, stored)
    elif reply.get("code") == "rate_limited":
        recorder.limited()
    else:
        recorder.error(f"ws_{reply.get('code', 'unknown')}")


class _StopWorker(Exception):
    """The connection is unusable; the worker stops sending."""


async def _drive(scenario: Scenario, recorder: Recorder, send: SendFn, t0: float) -> None:
    """Call ``send`` on the scenario's schedule, or back-to-back when unpaced.

    All batches of one burst go to ``send`` together so the worker can put
    them on the wire at once instead of one response at a time.
    """
    factory = ReadingFactory()
    if scenario.paced:
        # A send is "late" when it starts more than one interval behind plan,
        # i.e. the client (or a slow server) cannot keep the requested rate.
        slack = (1.0 / scenario.rate) if scenario.rate else scenario.burst_every_s
        for offset, size in scenario.send_groups():
            delay = t0 + offset - time.monotonic()
            if delay > 0:
                await asyncio.sleep(delay)
            elif -delay > slack:
                recorder.late()
            await send([factory.batch(scenario.batch_size) for _ in range(size)])
        # The last send is planned one interval before the end; wait out the rest so
        # elapsed time (and so throughput) covers the same duration as the target.
        remaining = t0 + scenario.duration_s - time.monotonic()
        if remaining > 0:
            await asyncio.sleep(remaining)
    else:
        deadline = t0 + scenario.duration_s
        while time.monotonic() < deadline:
            await send([factory.batch(scenario.batch_size)])


class _StartGate:
    """Shared start line: the clock starts once every worker has arrived.

    WS workers arrive after their handshake (success or failure), so the
    configured duration and pacing cover ingest traffic, not connection setup.
    """

    def __init__(self, workers: int) -> None:
        self._waiting = workers
        self._open = asyncio.Event()
        self.t0 = 0.0

    async def arrive(self) -> float:
        self._waiting -= 1
        if self._waiting <= 0 and not self._open.is_set():
            self.t0 = time.monotonic()
            self._open.set()
        await self._open.wait()
        return self.t0


_Worker = Callable[
    [aiohttp.ClientSession, str, SensorCredential, Scenario, Recorder, _StartGate, float],
    Awaitable[None],
]


async def _rest_worker(
    session: aiohttp.ClientSession,
    base_url: str,
    cred: SensorCredential,
    scenario: Scenario,
    recorder: Recorder,
    gate: _StartGate,
    timeout_s: float,
) -> None:
    del timeout_s  # REST requests are bounded by the session timeout
    url = f"{base_url}/api/v1/telemetry"
    headers = {"Authorization": f"Bearer {cred.token}"}

    async def post(readings: Batch) -> None:
        body = {"sensor_id": cred.sensor_id, "readings": readings}
        started = time.perf_counter()
        try:
            async with session.post(url, json=body, headers=headers) as resp:
                raw = await resp.read()
                latency_ms = (time.perf_counter() - started) * 1000
                if resp.status == 202:
                    try:
                        reply = json.loads(raw)
                    except ValueError:
                        reply = None
                    stored = _accepted_count(reply, len(readings))
                    if stored is None:
                        recorder.error("http_202_malformed")
                    else:
                        recorder.accepted(latency_ms, stored)
                elif resp.status == 429:
                    recorder.limited()
                else:
                    recorder.error(f"http_{resp.status}")
        except asyncio.TimeoutError:
            recorder.error("timeout")
        except aiohttp.ClientError as exc:
            recorder.error(type(exc).__name__)

    async def send(batches: list[Batch]) -> None:
        await asyncio.gather(*(post(b) for b in batches))

    await _drive(scenario, recorder, send, await gate.arrive())


async def _ws_worker(
    session: aiohttp.ClientSession,
    base_url: str,
    cred: SensorCredential,
    scenario: Scenario,
    recorder: Recorder,
    gate: _StartGate,
    timeout_s: float,
) -> None:
    url = f"{base_url}/api/v1/telemetry/ws?sensor_id={cred.sensor_id}"
    headers = {"Authorization": f"Bearer {cred.token}"}
    try:
        ws = await session.ws_connect(
            url, headers=headers, timeout=aiohttp.ClientWSTimeout(ws_close=timeout_s)
        )
    except aiohttp.WSServerHandshakeError as exc:
        recorder.error(f"ws_handshake_{exc.status}")
        recorder.abort()
        await gate.arrive()
        return
    except (aiohttp.ClientError, asyncio.TimeoutError) as exc:
        recorder.error(f"ws_connect_{type(exc).__name__}")
        recorder.abort()
        await gate.arrive()
        return

    seq = count()

    async def send(batches: list[Batch]) -> None:
        # The server answers every text frame exactly once, in order, so a
        # burst is pipelined: write all frames, then read the replies in order.
        sent: list[tuple[float, int]] = []
        try:
            for readings in batches:
                frame = json.dumps({"readings": readings, "seq": next(seq)})
                sent.append((time.perf_counter(), len(readings)))
                await ws.send_str(frame)
            for started, size in sent:
                reply = await ws.receive_json(timeout=timeout_s)
                _record_ws_reply(recorder, reply, (time.perf_counter() - started) * 1000, size)
        except asyncio.TimeoutError:
            # A late ack would be matched to the next frame, so stop here.
            recorder.error("timeout")
            raise _StopWorker from None
        except (aiohttp.ClientError, TypeError, ValueError) as exc:
            # TypeError: a non-text frame (e.g. CLOSE) arrived instead of JSON.
            recorder.error(f"ws_{type(exc).__name__}")
            raise _StopWorker from None

    try:
        await _drive(scenario, recorder, send, await gate.arrive())
    except _StopWorker:
        # The rest of this sensor's schedule is not sent; the report fails on it.
        recorder.abort()
    finally:
        await ws.close()


async def run_scenario(
    scenario: Scenario,
    credentials: list[SensorCredential],
    base_url: str,
    *,
    request_timeout_s: float = 10.0,
) -> Report:
    # aiohttp treats a zero timeout as "no timeout", so a stalled server would hang the run.
    if not (math.isfinite(request_timeout_s) and request_timeout_s > 0):
        raise ValueError("request timeout must be a finite number of seconds > 0")
    if len(credentials) < scenario.sensors:
        raise ValueError(
            f"scenario {scenario.name!r} needs {scenario.sensors} sensors, "
            f"only {len(credentials)} credentials given"
        )
    base_url = base_url.rstrip("/")
    worker: _Worker = _rest_worker if scenario.transport == "rest" else _ws_worker
    recorder = Recorder()
    connector = aiohttp.TCPConnector(limit=0)
    # The session timeout bounds each REST request and the WS handshake; it
    # does not cut an upgraded WebSocket, whose acks use ``receive_json(timeout=)``.
    timeout = aiohttp.ClientTimeout(total=request_timeout_s)
    async with aiohttp.ClientSession(connector=connector, timeout=timeout) as session:
        gate = _StartGate(scenario.sensors)
        await asyncio.gather(
            *(
                worker(session, base_url, cred, scenario, recorder, gate, request_timeout_s)
                for cred in credentials[: scenario.sensors]
            )
        )
        elapsed = time.monotonic() - gate.t0
    return build_report(scenario, recorder, elapsed)
