import {
  clamp,
  type PersistedSettings,
  type TelemetryIngestBody,
  type TelemetryIngestReading,
  type SensorConfig,
} from "./domain";

export interface GeneratorRuntime {
  sequence: number;
  lastTimestampMs: number;
  rngState: number;
  seed: number;
}

export const hashStringToUint32 = (str: string) => {
  let h = 2166136261;
  for (let i = 0; i < str.length; i++) {
    h ^= str.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return h >>> 0;
};

export function mulberry32(seed: number): () => number {
  let t = seed >>> 0;
  return () => {
    t += 0x6d2b79f5;
    let r = Math.imul(t ^ (t >>> 15), 1 | t);
    r ^= r + Math.imul(r ^ (r >>> 7), 61 | r);
    return ((r ^ (r >>> 14)) >>> 0) / 4294967296;
  };
}

export function waveformValue(
  waveform: PersistedSettings["waveform"],
  tSec: number,
  amplitude: number,
  periodSec: number,
  dutyCycle: number,
) {
  const a = Math.abs(amplitude);
  const p = Math.max(0.001, periodSec);
  const phase = (((tSec % p) + p) % p) / p;
  if (waveform === "sine") return Math.sin(2 * Math.PI * phase) * a;
  if (waveform === "saw") return (2 * phase - 1) * a;
  return phase < clamp(dutyCycle, 0, 1) ? a : 0;
}

export function scenarioIsPausedAt(s: PersistedSettings, nowSec: number) {
  if (s.scenario !== "dropout") return false;
  const cycle = s.dropoutEverySec + s.dropoutDurationSec;
  return cycle > 0 && nowSec % cycle >= s.dropoutEverySec;
}

export function scenarioEffectiveRate(s: PersistedSettings, nowSec: number) {
  if (s.scenario === "bursts") {
    const cycle = s.burstEverySec + s.burstDurationSec;
    if (cycle > 0 && nowSec % cycle >= s.burstEverySec)
      return clamp(s.rateHz * 8, 1, 10000);
  }
  return s.rateHz;
}

export function buildReadings(
  sensorKey: string,
  n: number,
  rateHz: number,
  settings: PersistedSettings,
  runtime: GeneratorRuntime,
  isContinuous: boolean,
  sendStartMs?: number,
  now = Date.now(),
): TelemetryIngestReading[] {
  const step = 1000 / clamp(rateHz, 1, 10000);
  const rng = mulberry32((settings.seed >>> 0) ^ hashStringToUint32(sensorKey));
  let base =
    runtime.lastTimestampMs > 0
      ? runtime.lastTimestampMs + step
      : (sendStartMs ?? now);
  if (!isContinuous && !sendStartMs) base = now;
  const readings: TelemetryIngestReading[] = [];
  for (let i = 0; i < n; i++) {
    const tMs = base + i * step;
    const raw =
      waveformValue(
        settings.waveform,
        tMs / 1000,
        clamp(settings.amplitude, 0, 1000000),
        clamp(settings.periodSec, 0.001, 1000000),
        settings.dutyCycle,
      ) +
      (rng() - 0.5) * 1.5 +
      20;
    readings.push({
      timestamp: new Date(
        tMs -
          (settings.scenario === "late_data" ? settings.lateSeconds * 1000 : 0),
      ).toISOString(),
      raw_value: raw,
      physical_value: raw,
      meta: {
        seq: runtime.sequence++,
        scenario: settings.scenario,
        generated_at: new Date(now).toISOString(),
      },
    });
  }
  runtime.lastTimestampMs = base + (n - 1) * step;
  runtime.rngState = 1;
  if (settings.scenario === "out_of_order") {
    const swaps = Math.floor(
      readings.length * clamp(settings.outOfOrderFraction, 0, 1),
    );
    for (let i = 0; i < swaps; i++) {
      const a = Math.floor(rng() * readings.length),
        b = Math.floor(rng() * readings.length);
      [readings[a], readings[b]] = [readings[b], readings[a]];
    }
  }
  return readings;
}

export function assemblePayload(
  sensor: SensorConfig,
  n: number,
  rateHz: number,
  settings: PersistedSettings,
  readings: TelemetryIngestReading[],
): TelemetryIngestBody {
  return {
    sensor_id: sensor.sensorId.trim(),
    run_id: sensor.runId.trim() || null,
    capture_session_id: sensor.captureSessionId.trim() || null,
    meta: {
      source: "sensor-simulator-web",
      scenario: settings.scenario,
      rate_hz: rateHz,
      batch_size: n,
      signal: {
        waveform: settings.waveform,
        amplitude: clamp(settings.amplitude, 0, 1000000),
        period_sec: clamp(settings.periodSec, 0.001, 1000000),
        duty_cycle:
          settings.waveform === "pulses"
            ? clamp(settings.dutyCycle, 0, 1)
            : null,
      },
    },
    readings,
  };
}
