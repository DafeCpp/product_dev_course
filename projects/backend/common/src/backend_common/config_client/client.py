"""Config-service consumer SDK.

Priority chain (highest → lowest):
  1. ENV-override  ``CONFIG_OVERRIDE__<SERVICE>__<KEY>``
  2. In-process cache (last successful 200 from bulk polling)
  3. Fallback file  ``<fallback_dir>/<service>.json`` (cold-start)
  4. Caller-supplied *default*

Usage::

    client = ConfigClient("telemetry-ingest", "http://config-service:8005")

    # aiohttp lifecycle:
    app.on_startup.append(client.start)
    app.on_cleanup.append(client.stop)

    # Read a value:
    max_req = client.get("rest_rate_limit", default=600)

    # Subscribe to changes:
    client.subscribe(lambda configs: print("updated:", configs))
"""
from __future__ import annotations

import asyncio
import json
import os
import random
import time
from email.utils import parsedate_to_datetime
from pathlib import Path
from typing import Any, Callable

import aiohttp
import structlog
from prometheus_client import Counter, Histogram

logger = structlog.get_logger(__name__)

_POLL_TOTAL: Counter = Counter(
    "config_client_polls_total",
    "Total config-service bulk poll attempts",
    ["service", "status"],
)
_PROPAGATION_LAG: Histogram = Histogram(
    "config_client_propagation_lag_seconds",
    "Seconds between config change on server and client applying it",
    ["service"],
    buckets=[0.5, 1, 2, 5, 10, 30, 60, 120],
)


def _env_var_name(service_name: str, key: str) -> str:
    """Return the ENV-override variable name for a given service/key pair.

    Example: service="telemetry-ingest", key="rest_rate_limit"
             → "CONFIG_OVERRIDE__TELEMETRY_INGEST__REST_RATE_LIMIT"
    """
    svc = service_name.upper().replace("-", "_").replace(".", "_")
    k = key.upper().replace(".", "_").replace("-", "_")
    return f"CONFIG_OVERRIDE__{svc}__{k}"


def _coerce(raw: str) -> Any:
    """Parse an ENV-override string as JSON; fall back to the raw string."""
    try:
        return json.loads(raw)
    except (json.JSONDecodeError, ValueError):
        return raw


class ConfigClient:
    """Async consumer SDK for config-service bulk polling.

    The client is lifecycle-compatible with aiohttp applications::

        app.on_startup.append(client.start)
        app.on_cleanup.append(client.stop)

    It can also be managed manually::

        await client.start()
        ...
        await client.stop()

    Args:
        service_name: Logical service identifier (e.g. ``"telemetry-ingest"``).
        config_service_url: Base URL of the config-service (no trailing slash).
        poll_interval: Nominal polling interval in seconds (default 1 s).
            Actual sleep is ``poll_interval * (1 ± 0.25)`` (jitter).
        fallback_dir: Directory for the fallback JSON file.
            Defaults to ``/var/cache/config-service``.
        session: Optional pre-built ``aiohttp.ClientSession`` to reuse.
            When *None* (default) the client creates and owns its own session.
    """

    def __init__(
        self,
        service_name: str,
        config_service_url: str,
        *,
        poll_interval: float = 1.0,
        fallback_dir: str | Path = "/var/cache/config-service",
        session: aiohttp.ClientSession | None = None,
    ) -> None:
        self.service_name = service_name
        self._url = f"{config_service_url.rstrip('/')}/api/v1/configs/bulk"
        self._poll_interval = poll_interval
        self._fallback_path = Path(fallback_dir) / f"{service_name}.json"
        self._external_session = session

        self._cache: dict[str, Any] = {}
        self._etag: str | None = None
        self._subscribers: list[Callable[[dict[str, Any]], Any]] = []
        self._task: asyncio.Task[None] | None = None
        self._session: aiohttp.ClientSession | None = None

        # Populate cache from fallback file so get() works before first poll.
        self._load_fallback()

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def get(self, key: str, *, default: Any = None) -> Any:
        """Return the config value for *key*, applying the priority chain.

        1. ENV-override ``CONFIG_OVERRIDE__<SERVICE>__<KEY>``
        2. In-process cache (from latest successful poll)
        3. *default*
        """
        env_val = os.environ.get(_env_var_name(self.service_name, key))
        if env_val is not None:
            return _coerce(env_val)

        if key in self._cache:
            return self._cache[key]

        return default

    def subscribe(self, callback: Callable[[dict[str, Any]], Any]) -> None:
        """Register *callback* to be called with the full configs dict on each update.

        The callback may be a regular function or a coroutine function.
        Exceptions raised by subscribers are logged and suppressed.
        """
        self._subscribers.append(callback)

    async def start(self, _app: Any = None) -> None:
        """Start the background polling loop.

        Compatible with ``app.on_startup``; the *_app* argument is ignored.
        Calling ``start()`` on an already-running client is a no-op.
        """
        if self._task is not None and not self._task.done():
            return
        if self._external_session is None:
            self._session = aiohttp.ClientSession()
        else:
            self._session = self._external_session
        self._task = asyncio.create_task(
            self._poll_loop(), name=f"config-client-{self.service_name}"
        )
        logger.info("config_client started", service=self.service_name, url=self._url)

    async def stop(self, _app: Any = None) -> None:
        """Stop the background polling loop and close the owned session.

        Compatible with ``app.on_cleanup``; the *_app* argument is ignored.
        """
        if self._task is not None:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
            self._task = None

        if self._session is not None and self._external_session is None:
            await self._session.close()
            self._session = None

        logger.info("config_client stopped", service=self.service_name)

    # ------------------------------------------------------------------
    # Polling internals
    # ------------------------------------------------------------------

    async def _poll_loop(self) -> None:
        backoff = 1.0
        max_backoff = 60.0

        while True:
            try:
                jitter = 1.0 + random.uniform(-0.25, 0.25)
                await asyncio.sleep(self._poll_interval * jitter)
                await self._poll_once()
                backoff = 1.0
            except asyncio.CancelledError:
                raise
            except Exception:
                logger.exception(
                    "config_client poll error — backing off",
                    service=self.service_name,
                    backoff_seconds=backoff,
                )
                _POLL_TOTAL.labels(service=self.service_name, status="error").inc()
                jitter = 1.0 + random.uniform(-0.25, 0.25)
                await asyncio.sleep(backoff * jitter)
                backoff = min(backoff * 2.0, max_backoff)

    async def _poll_once(self) -> None:
        assert self._session is not None, "start() must be called before polling"

        headers: dict[str, str] = {}
        if self._etag:
            headers["If-None-Match"] = self._etag

        async with self._session.get(
            self._url,
            params={"service": self.service_name},
            headers=headers,
            timeout=aiohttp.ClientTimeout(total=5.0),
        ) as resp:
            if resp.status == 304:
                _POLL_TOTAL.labels(service=self.service_name, status="304").inc()
                return

            if resp.status != 200:
                _POLL_TOTAL.labels(service=self.service_name, status=str(resp.status)).inc()
                logger.warning(
                    "config_client unexpected poll status",
                    service=self.service_name,
                    status=resp.status,
                )
                return

            data: dict[str, Any] = await resp.json()
            new_configs: dict[str, Any] = data.get("configs", {})
            new_etag: str | None = resp.headers.get("ETag")
            last_modified_str: str | None = resp.headers.get("Last-Modified")

            if last_modified_str:
                try:
                    lm = parsedate_to_datetime(last_modified_str)
                    lag = max(0.0, time.time() - lm.timestamp())
                    _PROPAGATION_LAG.labels(service=self.service_name).observe(lag)
                except Exception:
                    pass

            # Atomic in-process state update (asyncio is single-threaded).
            self._cache = new_configs
            if new_etag:
                self._etag = new_etag

            _POLL_TOTAL.labels(service=self.service_name, status="200").inc()
            logger.info(
                "config_client applied update",
                service=self.service_name,
                keys=list(new_configs.keys()),
                etag=new_etag,
            )

            self._save_fallback(new_configs)

            for cb in list(self._subscribers):
                try:
                    result = cb(new_configs)
                    if asyncio.iscoroutine(result):
                        await result
                except Exception:
                    logger.exception(
                        "config_client subscriber raised", service=self.service_name
                    )

    # ------------------------------------------------------------------
    # Fallback file helpers (sync; small file, acceptable blocking)
    # ------------------------------------------------------------------

    def _load_fallback(self) -> None:
        try:
            if self._fallback_path.exists():
                self._cache = json.loads(self._fallback_path.read_text())
                logger.info(
                    "config_client loaded fallback file",
                    service=self.service_name,
                    path=str(self._fallback_path),
                )
        except Exception:
            logger.warning(
                "config_client fallback load failed",
                service=self.service_name,
                path=str(self._fallback_path),
            )

    def _save_fallback(self, configs: dict[str, Any]) -> None:
        try:
            self._fallback_path.parent.mkdir(parents=True, exist_ok=True)
            tmp = self._fallback_path.with_suffix(".tmp")
            tmp.write_text(json.dumps(configs, default=str))
            tmp.rename(self._fallback_path)
        except Exception:
            logger.warning(
                "config_client fallback save failed",
                service=self.service_name,
                path=str(self._fallback_path),
            )
