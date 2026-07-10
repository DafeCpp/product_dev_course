"""Эмулятор CANopen-узла клапана Atos (node 0x20) на сырых кадрах.

Осознанно НЕ использует CANopen-библиотеку: сырые кадры python-can
дают прозрачные таймстампы для измерений RTT и делают протокольное
поведение эмулятора явным (это «вторая реализация» напротив
CANopenNode-мастера). Поведение:

* bootup 0x720/0x00 при старте, heartbeat каждые 100 мс;
* NMT (0x000): Start/Stop для узла 0x20 или broadcast;
* setpoint-RPDO (0x220): int16 команда + control word → шаг модели;
* feedback-TPDO (0x1A0): force/position/status;
  - event-режим (по умолчанию): отправка сразу в ответ на setpoint —
    минимальная латентность, RTT измерим по паре 0x220→0x1A0;
  - sync-режим (--sync): отправка по приёму SYNC (0x080) —
    детерминированная фаза выборки;
* минимальный SDO-сервер (0x620→0x5A0): expedited download
  подтверждается (используется мастером для настройки режима),
  запись 0x1800:02 переключает event/sync.

Запуск:  python -m benchsim.valve_node --channel vcan0 [--sync]
         [--fail-at-s 3.0]
"""

from __future__ import annotations

import argparse
import struct
import time

import can

from .plant import Plant, force_to_raw, position_to_raw, raw_to_command

NODE_ID = 0x20
COB_NMT = 0x000
COB_SYNC = 0x080
COB_FEEDBACK = 0x180 + NODE_ID   # 0x1A0, TPDO1 узла
COB_SETPOINT = 0x200 + NODE_ID   # 0x220, RPDO1 узла
COB_SDO_RX = 0x600 + NODE_ID     # 0x620
COB_SDO_TX = 0x580 + NODE_ID     # 0x5A0
COB_HEARTBEAT = 0x700 + NODE_ID  # 0x720

NMT_OPERATIONAL = 0x05
NMT_PREOPERATIONAL = 0x7F

TRANSMISSION_SYNC = 0x01
TRANSMISSION_EVENT = 0xFE


class ValveNode:
    def __init__(self, bus: can.BusABC, sync_mode: bool = False) -> None:
        self.bus = bus
        self.plant = Plant()
        self.nmt_state = NMT_PREOPERATIONAL
        self.transmission = (TRANSMISSION_SYNC
                             if sync_mode else TRANSMISSION_EVENT)
        self.valve_cmd = 0.0
        self.enabled = False
        self.last_plant_step = time.monotonic()
        self.last_hb = 0.0
        self.rx_setpoints = 0
        self.tx_feedback = 0
        # Момент перехода в operational (NMT Start от мастера) — это и
        # есть "now_ms=0" в CSV контроллера (его цикл стартует сразу
        # после Init/NMT-старта). Таймер --fail-at-s считается отсюда,
        # а не от старта потока эмулятора: иначе задержка bootup-паузы
        # и запуска subprocess'а sim_host сдвигает событие раньше, чем
        # показывает время контроллера.
        self.became_operational_at: float | None = None

    def start(self) -> None:
        self._send(COB_HEARTBEAT, bytes([0x00]))  # bootup

    def _send(self, cob: int, data: bytes) -> None:
        self.bus.send(can.Message(arbitration_id=cob, data=data,
                                  is_extended_id=False))

    def _step_plant(self) -> None:
        now = time.monotonic()
        dt = now - self.last_plant_step
        self.last_plant_step = now
        cmd = self.valve_cmd if self.enabled else 0.0
        # Модель шагается редкими крупными dt (событийно) — дробим,
        # чтобы дискретизация золотника не отличалась от 500 Гц у C++.
        while dt > 0:
            sub = min(dt, 0.002)
            self.plant.step(cmd, sub)
            dt -= sub

    def _send_feedback(self) -> None:
        payload = struct.pack(
            "<hhB", force_to_raw(self.plant.force_n),
            position_to_raw(self.plant.position_mm), 0x07)
        self._send(COB_FEEDBACK, payload)
        self.tx_feedback += 1

    def _handle_sdo(self, data: bytes) -> None:
        if len(data) < 4:
            return
        ccs = data[0] >> 5
        index, sub = struct.unpack_from("<HB", data, 1)
        if ccs == 1:  # expedited download
            if index == 0x1800 and sub == 0x02 and len(data) >= 5:
                self.transmission = data[4]
            self._send(COB_SDO_TX,
                       bytes([0x60]) + data[1:4] + b"\x00\x00\x00\x00")
        elif ccs == 2:  # upload — отдаём нули (не используется ригом)
            self._send(COB_SDO_TX,
                       bytes([0x43]) + data[1:4] + b"\x00\x00\x00\x00")

    def handle(self, msg: can.Message) -> None:
        cob = msg.arbitration_id
        data = bytes(msg.data)

        if cob == COB_NMT and len(data) >= 2:
            if data[1] in (NODE_ID, 0x00):
                if data[0] == 0x01:
                    self.nmt_state = NMT_OPERATIONAL
                    if self.became_operational_at is None:
                        self.became_operational_at = time.monotonic()
                elif data[0] in (0x02, 0x80):
                    self.nmt_state = NMT_PREOPERATIONAL
        elif cob == COB_SETPOINT and self.nmt_state == NMT_OPERATIONAL:
            if len(data) >= 3:
                raw, control = struct.unpack_from("<hB", data)
                # Сначала докатываем модель со СТАРОЙ командой на весь
                # интервал простоя, и только потом применяем новую —
                # иначе первый setpoint после паузы (bootup, разрыв
                # событий) задним числом интегрируется так, будто
                # действовал всё время простоя.
                self._step_plant()
                self.valve_cmd = raw_to_command(raw)
                self.enabled = bool(control & 0x80)
                self.rx_setpoints += 1
                if self.transmission == TRANSMISSION_EVENT:
                    self._send_feedback()
        elif cob == COB_SYNC:
            if (self.nmt_state == NMT_OPERATIONAL
                    and self.transmission != TRANSMISSION_EVENT):
                self._step_plant()
                self._send_feedback()
        elif cob == COB_SDO_RX:
            self._handle_sdo(data)

    def tick(self) -> None:
        now = time.monotonic()
        if now - self.last_hb >= 0.1:
            self.last_hb = now
            self._send(COB_HEARTBEAT, bytes([self.nmt_state]))


def run(channel: str, sync_mode: bool, fail_at_s: float,
        duration_s: float) -> None:
    bus = can.interface.Bus(channel=channel, interface="socketcan")
    node = ValveNode(bus, sync_mode=sync_mode)
    node.start()
    started = time.monotonic()
    failed = False
    try:
        while duration_s <= 0 or time.monotonic() - started < duration_s:
            msg = bus.recv(timeout=0.01)
            if msg is not None and not msg.is_error_frame:
                node.handle(msg)
            node.tick()
            # Отсчёт от NMT-старта (≈ момент, когда контроллер начинает
            # свой цикл), а не от запуска потока эмулятора — иначе
            # bootup-пауза и старт subprocess'а sim_host сдвигают
            # событие раньше, чем показывает CSV-время контроллера.
            if (fail_at_s > 0 and not failed
                    and node.became_operational_at is not None
                    and time.monotonic() - node.became_operational_at
                    >= fail_at_s):
                node.plant.trigger_failure()
                failed = True
    finally:
        bus.shutdown()
        print(f"valve_node: rx_setpoints={node.rx_setpoints} "
              f"tx_feedback={node.tx_feedback}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--channel", default="vcan0")
    parser.add_argument("--sync", action="store_true",
                        help="feedback по SYNC вместо event-driven")
    parser.add_argument("--fail-at-s", type=float, default=0.0)
    parser.add_argument("--duration-s", type=float, default=0.0,
                        help="0 = до Ctrl+C")
    args = parser.parse_args()
    run(args.channel, args.sync, args.fail_at_s, args.duration_s)


if __name__ == "__main__":
    main()
