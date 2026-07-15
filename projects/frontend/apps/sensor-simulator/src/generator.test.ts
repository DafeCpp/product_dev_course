import { describe, expect, it } from 'vitest'
import { DEFAULT_SETTINGS } from './domain'
import { assemblePayload, buildReadings, scenarioEffectiveRate, scenarioIsPausedAt, waveformValue, type GeneratorRuntime } from './generator'
const runtime = (): GeneratorRuntime => ({ sequence: 0, lastTimestampMs: 0, rngState: 0, seed: 42 })
describe('sensor generator', () => {
  it('supports waveform boundaries', () => { expect(waveformValue('sine', 0, 10, 5, .1)).toBeCloseTo(0); expect(waveformValue('pulses', 0, 10, 5, .1)).toBe(10); expect(waveformValue('pulses', 1, 10, 5, .1)).toBe(0); expect(waveformValue('saw', 0, 10, 5, .1)).toBe(-10) })
  it('is deterministic for the same sensor and seed', () => { const a = buildReadings('sensor', 5, 10, DEFAULT_SETTINGS, runtime(), false, 1000, 1000); const b = buildReadings('sensor', 5, 10, DEFAULT_SETTINGS, runtime(), false, 1000, 1000); expect(a).toEqual(b) })
  it('generates late timestamps and out-of-order timestamps', () => { const late = buildReadings('s', 2, 10, { ...DEFAULT_SETTINGS, scenario: 'late_data', lateSeconds: 60 }, runtime(), false, 100000, 100000); expect(Date.parse(late[0].timestamp)).toBe(40000); const out = buildReadings('s', 20, 10, { ...DEFAULT_SETTINGS, scenario: 'out_of_order', outOfOrderFraction: 1 }, runtime(), false, 100000, 100000); expect(out.map(x => x.timestamp).join()).not.toBe(out.map(x => x.timestamp).sort().join()) })
  it('models burst and dropout scenarios', () => { expect(scenarioEffectiveRate({ ...DEFAULT_SETTINGS, scenario: 'bursts' }, 12)).toBe(80); expect(scenarioIsPausedAt({ ...DEFAULT_SETTINGS, scenario: 'dropout' }, 18)).toBe(true) })
  it('assembles the ingest contract', () => { const sensor = { key: 'k', label: '', sensorId: 'id', sensorToken: 'token', runId: '', captureSessionId: '', streamSinceId: 0, settings: DEFAULT_SETTINGS }; expect(assemblePayload(sensor, 1, 10, DEFAULT_SETTINGS, []).readings).toEqual([]); expect(assemblePayload(sensor, 1, 10, DEFAULT_SETTINGS, []).run_id).toBeNull() })
})
