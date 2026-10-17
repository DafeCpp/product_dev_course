"""Mock-WS для страницы отладки bench-head (LOS-187).

Отдаёт статику страницы и WebSocket /ws: снимки по схеме Ф2 (25 Гц) и приём
команд. Модель — model.py. Запуск:

    pip install -r requirements.txt
    python mock_server.py                       # http://localhost:8090
    python mock_server.py --bus-mode monitor    # форма команд заблокирована
    python mock_server.py --program             # идёт программа: ручной ввод заблокирован
    python mock_server.py --no-frames           # «кадров на шине нет»
    python mock_server.py --link-loss-at 20     # потеря связи на 20-й секунде
"""

from __future__ import annotations

import argparse
import asyncio
import json
from pathlib import Path

from aiohttp import WSMsgType, web

from model import BenchModel, ParseError, Scenario, parse_command

STATIC_DIR = Path(__file__).resolve().parent.parent
TICK_S = 0.04  # 25 Гц


async def ws_handler(request: web.Request) -> web.WebSocketResponse:
    ws = web.WebSocketResponse(heartbeat=10)
    await ws.prepare(request)
    model: BenchModel = request.app["model"]
    request.app["clients"].add(ws)
    try:
        async for msg in ws:
            if msg.type != WSMsgType.TEXT:
                continue
            try:
                model.post(parse_command(json.loads(msg.data)))
            except (ParseError, json.JSONDecodeError) as exc:
                code = exc.code if isinstance(exc, ParseError) else "syntax"
                await ws.send_json({"v": 1, "type": "error", "error": code})
    finally:
        request.app["clients"].discard(ws)
    return ws


async def ticker(app: web.Application) -> None:
    model: BenchModel = app["model"]
    while True:
        await asyncio.sleep(TICK_S)
        model.step(TICK_S)
        text = json.dumps(model.snapshot())
        for ws in list(app["clients"]):
            if not ws.closed:
                await ws.send_str(text)


async def start_ticker(app: web.Application) -> None:
    app["ticker"] = asyncio.create_task(ticker(app))


async def stop_ticker(app: web.Application) -> None:
    app["ticker"].cancel()


async def index(_: web.Request) -> web.FileResponse:
    return web.FileResponse(STATIC_DIR / "index.html")


def make_app(scenario: Scenario) -> web.Application:
    app = web.Application()
    app["model"] = BenchModel(scenario=scenario)
    app["clients"] = set()
    app.router.add_get("/", index)
    app.router.add_get("/ws", ws_handler)
    app.router.add_static("/", STATIC_DIR, show_index=False)
    app.on_startup.append(start_ticker)
    app.on_cleanup.append(stop_ticker)
    return app


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=8090)
    p.add_argument("--bus-mode", choices=["bring_up_master", "monitor"], default="bring_up_master")
    p.add_argument("--no-frames", action="store_true", help="на шине нет кадров")
    p.add_argument("--program", action="store_true", help="стартовать с идущей программой")
    p.add_argument("--link-loss-at", type=float, default=None, metavar="SEC")
    p.add_argument("--estop-chain", choices=["unknown", "ok", "open"], default="unknown")
    a = p.parse_args()
    scenario = Scenario(bus_mode=a.bus_mode, frames_seen=not a.no_frames, program=a.program,
                        link_loss_at_s=a.link_loss_at, estop_chain=a.estop_chain)
    web.run_app(make_app(scenario), host=a.host, port=a.port)


if __name__ == "__main__":
    main()
