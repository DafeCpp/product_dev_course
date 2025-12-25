from __future__ import annotations

import argparse
import asyncio
import signal

from telemetry_cli.config import load_config
from telemetry_cli.runner import run_agent


def main() -> None:
    parser = argparse.ArgumentParser(prog="telemetry-cli")
    parser.add_argument("--config", required=True, help="Path to YAML config")
    args = parser.parse_args()

    cfg = load_config(args.config)

    async def _main() -> None:
        stop = asyncio.Event()

        def _handle_stop(*_args) -> None:  # noqa: ANN001
            stop.set()

        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            try:
                loop.add_signal_handler(sig, _handle_stop)
            except NotImplementedError:
                pass

        task = asyncio.create_task(run_agent(cfg))
        await stop.wait()
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            return

    asyncio.run(_main())


