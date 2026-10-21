"""Async workers that drive REST and WebSocket ingest."""
from __future__ import annotations

import asyncio
import json
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


class ReadingFactory:
    """Builds batches with strictly increasing timestamps per sensor.

    Unique timestamps keep the dedup index from silently dropping readings,
    so ``accepted`` counts match what the service stored.
    """

    def __init__(self, start: datetime | None = None) -> None:
        self._start = start or datetime.now(timezone.utc)
        self._seq = count()

    def batch(self, size: int) -> list[dict[str, Any]]:
        readings = []
        for _ in range(size):
            n = next(self._seq)
            ts = self._start + timedelta(microseconds=n)
            readings.append(
                {
                    "timestamp": ts.isoformat(timespec="microseconds").replace("+00:00", "Z"),
                    "raw_value": float(n % 1000),
                    "meta": {"signal": "loadtest"},
                }
            )
        return readings


Batch = list[dict[str, Any]]
# Sends a group of batches planned for the same instant (one batch outside a burst).
SendFn = Callable[[list[Batch]], Awaitable[None]]


def _accepted_count(reply: Any, sent: int) -> int:
    """Readings the service reports as stored; dedup can make it lower than ``sent``."""
    if isinstance(reply, dict):
        value = reply.get("accepted")
        if isinstance(value, int) and not isinstance(value, bool):
            return value
    return sent


class _StopWorker(Exception):
    """The connection is unusable; the worker stops sending."""


def _group_offsets(offsets: list[float]) -> list[tuple[float, int]]:
    """Collapse equal consecutive offsets into ``(offset, count)`` groups."""
    groups: list[tuple[float, int]] = []
    for offset in offsets:
        if groups and groups[-1][0] == offset:
            groups[-1] = (offset, groups[-1][1] + 1)
        else:
            groups.append((offset, 1))
    return groups


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
        for offset, size in _group_offsets(scenario.send_offsets()):
            delay = t0 + offset - time.monotonic()
            if delay > 0:
                await asyncio.sleep(delay)
            elif -delay > slack:
                recorder.late()
            await send([factory.batch(scenario.batch_size) for _ in range(size)])
    else:
        deadline = t0 + scenario.duration_s
        while time.monotonic() < deadline:
            await send([factory.batch(scenario.batch_size)])


_Worker = Callable[
    [aiohttp.ClientSession, str, SensorCredential, Scenario, Recorder, float, float],
    Awaitable[None],
]


async def _rest_worker(
    session: aiohttp.ClientSession,
    base_url: str,
    cred: SensorCredential,
    scenario: Scenario,
    recorder: Recorder,
    t0: float,
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
                    recorder.accepted(latency_ms, _accepted_count(reply, len(readings)))
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

    await _drive(scenario, recorder, send, t0)


async def _ws_worker(
    session: aiohttp.ClientSession,
    base_url: str,
    cred: SensorCredential,
    scenario: Scenario,
    recorder: Recorder,
    t0: float,
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
        return
    except (aiohttp.ClientError, asyncio.TimeoutError) as exc:
        recorder.error(f"ws_connect_{type(exc).__name__}")
        recorder.abort()
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
                latency_ms = (time.perf_counter() - started) * 1000
                if reply.get("status") == "accepted":
                    recorder.accepted(latency_ms, _accepted_count(reply, size))
                elif reply.get("code") == "rate_limited":
                    recorder.limited()
                else:
                    recorder.error(f"ws_{reply.get('code', 'unknown')}")
        except asyncio.TimeoutError:
            # A late ack would be matched to the next frame, so stop here.
            recorder.error("timeout")
            raise _StopWorker from None
        except (aiohttp.ClientError, TypeError, ValueError) as exc:
            # TypeError: a non-text frame (e.g. CLOSE) arrived instead of JSON.
            recorder.error(f"ws_{type(exc).__name__}")
            raise _StopWorker from None

    try:
        await _drive(scenario, recorder, send, t0)
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
        t0 = time.monotonic()
        await asyncio.gather(
            *(
                worker(session, base_url, cred, scenario, recorder, t0, request_timeout_s)
                for cred in credentials[: scenario.sensors]
            )
        )
        elapsed = time.monotonic() - t0
    return build_report(scenario, recorder, elapsed)
