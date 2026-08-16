import {readFileSync} from 'node:fs';

import {describe, expect, it} from 'vitest';

import {
    EVENT_TYPE_NAMES,
    LogParseError,
    assignEventsToFrames,
    eventParamDesc,
    parseBinaryLog,
} from '../app.js';

const FRAME_SIZE = 132;
const EVENT_SIZE = 16;
const SNAPSHOT_SIZE = 268;

function event(ts, name = 'TestStart', desc = '', v1 = '', v2 = '') {
    return {ts, name, desc, v1, v2};
}

function setFrame(view, base, frame = {}) {
    view.setUint32(base, frame.ts ?? 0, true);
    view.setFloat32(base + 4, frame.ax ?? 0, true);
    view.setFloat32(base + 40, frame.speed ?? 0, true);
    view.setUint8(base + 124, frame.testMarker ?? 0);
    view.setUint8(base + 129, frame.kidsFlags ?? 0);
    view.setUint8(base + 130, frame.magFlags ?? 0);
}

function setEvent(view, base, record) {
    view.setUint32(base, record.ts, true);
    view.setUint8(base + 4, record.typeId);
    view.setUint8(base + 5, record.param ?? 0);
    view.setFloat32(base + 8, record.value1 ?? 0, true);
    view.setFloat32(base + 12, record.value2 ?? 0, true);
}

function setSnapshot(view, base, snapshot) {
    view.setUint32(base, snapshot.ts, true);
    view.setUint16(base + 4, snapshot.schemaVersion ?? 2, true);
    view.setUint16(base + 6, 64, true);
    const values = snapshot.values ?? [];
    for (let index = 0; index < 64; index++) {
        view.setFloat32(base + 8 + index * 4, values[index] ?? 0, true);
    }
    view.setUint32(base + 264, snapshot.frameIndex ?? 0, true);
}

function buildLog({frames, events = [], snapshots = null}) {
    const eventsBytes = 8 + events.length * EVENT_SIZE;
    const snapshotsBytes = snapshots === null
        ? 0
        : 8 + snapshots.length * SNAPSHOT_SIZE;
    const buffer = new ArrayBuffer(
        8 + frames.length * FRAME_SIZE + eventsBytes + snapshotsBytes);
    const view = new DataView(buffer);
    view.setUint32(0, frames.length, true);
    view.setUint32(4, FRAME_SIZE, true);
    frames.forEach((frame, index) => setFrame(
        view, 8 + index * FRAME_SIZE, frame));

    let cursor = 8 + frames.length * FRAME_SIZE;
    view.setUint32(cursor, events.length, true);
    view.setUint32(cursor + 4, EVENT_SIZE, true);
    events.forEach((record, index) => setEvent(
        view, cursor + 8 + index * EVENT_SIZE, record));
    cursor += eventsBytes;

    if (snapshots !== null) {
        view.setUint32(cursor, snapshots.length, true);
        view.setUint32(cursor + 4, SNAPSHOT_SIZE, true);
        snapshots.forEach((snapshot, index) => setSnapshot(
            view, cursor + 8 + index * SNAPSHOT_SIZE, snapshot));
    }
    return buffer;
}

describe('eventParamDesc and event names', () => {
    it('decodes event-specific parameters and numeric fallbacks', () => {
        expect(eventParamDesc(13, 1)).toBe('Straight');
        expect(eventParamDesc(16, 3)).toBe('Step');
        expect(eventParamDesc(13, 9)).toBe('9');
        expect(eventParamDesc(1, 2)).toBe('auto_forward');
        expect(eventParamDesc(2, 2)).toBe('stage2');
        expect(eventParamDesc(4, 0)).toBe('');
        expect(eventParamDesc(4, 7)).toBe('7');
    });

    it('matches every TelemetryEventType value in the firmware header', () => {
        const header = readFileSync(new URL(
            '../../../common/telemetry_event_log.hpp', import.meta.url), 'utf8');
        const enumBody = header.match(
            /enum class TelemetryEventType[^\{]*\{([\s\S]*?)\n\};/)?.[1];
        expect(enumBody).toBeDefined();
        const entries = [...enumBody.matchAll(/^\s*(\w+)\s*=\s*(\d+)\s*,/gm)]
            .map(([, name, value]) => [value, name]);
        expect(Object.fromEntries(entries)).toEqual(EVENT_TYPE_NAMES);
    });
});

describe('assignEventsToFrames', () => {
    it('returns an empty map when either input is empty', () => {
        expect(assignEventsToFrames([], [100])).toEqual(new Map());
        expect(assignEventsToFrames([event(100)], [])).toEqual(new Map());
    });

    it('assigns exact, intermediate, and near-tail events to frames', () => {
        const result = assignEventsToFrames([
            event(100, 'exact'),
            event(101, 'between'),
            event(119, 'next'),
            event(121, 'tail'),
            event(200, 'too-far'),
        ], [100, 110, 120]);
        expect(result.get(0).name).toBe('exact');
        expect(result.get(1).name).toBe('between');
        expect(result.get(2).name).toBe('next|tail');
        expect([...result.values()].some(value => value.name.includes('too-far')))
            .toBe(false);
    });

    it('keeps a small lead allowance but drops stale events', () => {
        const result = assignEventsToFrames([
            event(70, 'edge'), event(69, 'stale'), event(100, 'current'),
        ], [100, 110, 120]);
        expect(result.get(0).name).toBe('edge|current');
    });

    it('merges all event fields and configs with pipe separators', () => {
        const result = assignEventsToFrames([
            {...event(101, 'A', 'one', '1', '2'), configs: [{a: 1}]},
            {...event(102, 'B', 'two', '3', '4'), configs: [{b: 2}]},
        ], [100, 110]);
        expect(result.get(1)).toEqual({
            name: 'A|B', desc: 'one|two', v1: '1|3', v2: '2|4',
            configs: [{a: 1}, {b: 2}],
        });
    });

    it('preserves ordering across uint32 timestamp rollover', () => {
        const result = assignEventsToFrames([
            event(5, 'after-rollover'), event(0xffff_fffd, 'before-rollover'),
        ], [0xffff_fff8, 2, 12]);
        expect(result.get(1).name).toBe('before-rollover');
        expect(result.get(2).name).toBe('after-rollover');
    });
});

describe('parseBinaryLog', () => {
    it('parses frame fields, packed flags, and telemetry events', () => {
        const buffer = buildLog({
            frames: [
                {ts: 100, ax: 1.25, speed: 2.5, testMarker: 7,
                    kidsFlags: 0b1101, magFlags: 0b10},
                {ts: 110, ax: -0.5},
            ],
            events: [{ts: 105, typeId: 13, param: 2, value1: 4.5, value2: -0.25}],
        });
        const {csv, warnings} = parseBinaryLog(buffer);
        const [header, first, second] = csv.split('\n');
        const names = header.split(',');
        const firstValues = first.split(',');
        const secondValues = second.split(',');

        expect(warnings).toEqual([]);
        expect(firstValues[names.indexOf('ts_ms')]).toBe('100');
        expect(firstValues[names.indexOf('ax')]).toBe('1.25');
        expect(firstValues[names.indexOf('kids_anti_spin_active')]).toBe('1');
        expect(firstValues[names.indexOf('kids_accel_limit_active')]).toBe('0');
        expect(firstValues[names.indexOf('kids_speed_limit_active')]).toBe('1');
        expect(firstValues[names.indexOf('kids_limiters_enabled')]).toBe('1');
        expect(firstValues[names.indexOf('mag_gate_active')]).toBe('1');
        expect(secondValues[names.indexOf('event_type')]).toBe('TestStart');
        expect(secondValues[names.indexOf('event_param')]).toBe('Circle');
        expect(secondValues[names.indexOf('event_value1')]).toBe('4.5000');
        expect(secondValues[names.indexOf('event_value2')]).toBe('-0.2500');
    });

    it('places baseline and indexed configuration snapshots in CSV', () => {
        const baselineValues = Array(64).fill(0);
        baselineValues[0] = 1;
        baselineValues[1] = 3;
        baselineValues[63] = 1;
        const changedValues = baselineValues.slice();
        changedValues[1] = 4;
        const {csv, warnings} = parseBinaryLog(buildLog({
            frames: [{ts: 100}, {ts: 110}],
            snapshots: [
                {ts: 50, frameIndex: 0, values: baselineValues},
                {ts: 109, frameIndex: 1, values: changedValues},
            ],
        }));
        const [, first, second] = csv.split('\n');
        expect(warnings).toEqual([]);
        expect(first).toContain('StabilizationConfigSnapshot');
        expect(first).toContain('schema2');
        expect(first).toContain('"[{""enabled"":true');
        expect(second).toContain('StabilizationConfigSnapshot');
        expect(second).toContain('""mode"":4');
    });

    it('warns and skips an unsupported snapshot schema', () => {
        const {csv, warnings} = parseBinaryLog(buildLog({
            frames: [{ts: 100}],
            snapshots: [{ts: 100, schemaVersion: 99}],
        }));
        expect(warnings).toEqual([
            'Пропущен неподдерживаемый снапшот конфигурации 0',
        ]);
        expect(csv).not.toContain('StabilizationConfigSnapshot');
    });

    it('accepts a frame-only payload with no optional sections', () => {
        const buffer = new ArrayBuffer(8 + FRAME_SIZE);
        const view = new DataView(buffer);
        view.setUint32(0, 1, true);
        view.setUint32(4, FRAME_SIZE, true);
        setFrame(view, 8, {ts: 42});
        expect(parseBinaryLog(buffer).csv.split('\n')[1]).toMatch(/^42,/);
    });

    it.each([
        ['short file', new ArrayBuffer(7), 'Нет данных'],
        ['zero frames', (() => {
            const buffer = new ArrayBuffer(8);
            new DataView(buffer).setUint32(4, FRAME_SIZE, true);
            return buffer;
        })(), 'Нет данных телеметрии'],
        ['small frame', (() => {
            const buffer = new ArrayBuffer(8);
            const view = new DataView(buffer);
            view.setUint32(0, 1, true);
            view.setUint32(4, 130, true);
            return buffer;
        })(), 'Неподдерживаемый размер кадра'],
        ['truncated frames', (() => {
            const buffer = new ArrayBuffer(8 + FRAME_SIZE);
            const view = new DataView(buffer);
            view.setUint32(0, 2, true);
            view.setUint32(4, FRAME_SIZE, true);
            return buffer;
        })(), 'усечена секция кадров'],
        ['truncated event header', (() => {
            const valid = buildLog({frames: [{ts: 1}]});
            return valid.slice(0, 8 + FRAME_SIZE + 4);
        })(), 'усечён заголовок событий'],
        ['small event', (() => {
            const buffer = buildLog({frames: [{ts: 1}]});
            const view = new DataView(buffer);
            view.setUint32(8 + FRAME_SIZE + 4, 7, true);
            return buffer;
        })(), 'размер события меньше 8 байт'],
        ['truncated event', (() => {
            const buffer = new ArrayBuffer(8 + FRAME_SIZE + 8 + 8);
            const view = new DataView(buffer);
            view.setUint32(0, 1, true);
            view.setUint32(4, FRAME_SIZE, true);
            view.setUint32(8 + FRAME_SIZE, 1, true);
            view.setUint32(8 + FRAME_SIZE + 4, EVENT_SIZE, true);
            return buffer;
        })(), 'усечена секция событий'],
        ['truncated snapshot header', (() => {
            const valid = buildLog({frames: [{ts: 1}]});
            const extended = new Uint8Array(valid.byteLength + 4);
            extended.set(new Uint8Array(valid));
            return extended.buffer;
        })(), 'усечён заголовок снапшотов конфигурации'],
        ['truncated snapshot', (() => {
            const valid = buildLog({frames: [{ts: 1}], snapshots: [{ts: 1}]});
            return valid.slice(0, valid.byteLength - 1);
        })(), 'усечена секция снапшотов конфигурации'],
        ['unexpected trailing data', (() => {
            const valid = buildLog({frames: [{ts: 1}], snapshots: []});
            const extended = new Uint8Array(valid.byteLength + 1);
            extended.set(new Uint8Array(valid));
            return extended.buffer;
        })(), 'неожиданные данные в конце файла'],
    ])('rejects %s', (_, buffer, message) => {
        expect(() => parseBinaryLog(buffer)).toThrow(LogParseError);
        expect(() => parseBinaryLog(buffer)).toThrow(message);
    });

    it('rejects non-ArrayBuffer input', () => {
        expect(() => parseBinaryLog(new Uint8Array(8))).toThrow(TypeError);
    });
});
