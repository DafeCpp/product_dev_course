import {
  DEFAULT_SETTINGS,
  STORAGE_KEY,
  type PersistedSettings,
  type PersistedStateV2,
  type SensorConfig,
  type Scenario,
  type Waveform,
} from "./domain";

const randomKey = (prefix: string) =>
  `${prefix}_${globalThis.crypto?.randomUUID?.() ?? `${Date.now()}_${Math.random().toString(16).slice(2)}`}`;
const isScenario = (v: unknown): v is Scenario =>
  ["steady", "bursts", "dropout", "out_of_order", "late_data"].includes(
    v as string,
  );
const isWaveform = (v: unknown): v is Waveform =>
  ["sine", "pulses", "saw"].includes(v as string);
const number = (v: unknown, fallback: number) =>
  typeof v === "number" && Number.isFinite(v) ? v : fallback;
const string = (v: unknown) => (typeof v === "string" ? v : "");

export function createEmptySensor(): SensorConfig {
  return {
    key: randomKey("sensor"),
    label: "",
    sensorId: "",
    sensorToken: "",
    runId: "",
    captureSessionId: "",
    streamSinceId: 0,
    settings: { ...DEFAULT_SETTINGS },
  };
}
export function sanitizeSettings(raw: unknown): PersistedSettings {
  const s =
    raw && typeof raw === "object" ? (raw as Record<string, unknown>) : {};
  return {
    scenario: isScenario(s.scenario) ? s.scenario : DEFAULT_SETTINGS.scenario,
    rateHz: number(s.rateHz, DEFAULT_SETTINGS.rateHz),
    batchSize: number(s.batchSize, DEFAULT_SETTINGS.batchSize),
    seed: number(s.seed, DEFAULT_SETTINGS.seed),
    burstEverySec: number(s.burstEverySec, DEFAULT_SETTINGS.burstEverySec),
    burstDurationSec: number(
      s.burstDurationSec,
      DEFAULT_SETTINGS.burstDurationSec,
    ),
    dropoutEverySec: number(
      s.dropoutEverySec,
      DEFAULT_SETTINGS.dropoutEverySec,
    ),
    dropoutDurationSec: number(
      s.dropoutDurationSec,
      DEFAULT_SETTINGS.dropoutDurationSec,
    ),
    lateSeconds: number(s.lateSeconds, DEFAULT_SETTINGS.lateSeconds),
    outOfOrderFraction: number(
      s.outOfOrderFraction,
      DEFAULT_SETTINGS.outOfOrderFraction,
    ),
    waveform: isWaveform(s.waveform) ? s.waveform : DEFAULT_SETTINGS.waveform,
    amplitude: number(s.amplitude, DEFAULT_SETTINGS.amplitude),
    periodSec: number(s.periodSec, DEFAULT_SETTINGS.periodSec),
    dutyCycle: number(s.dutyCycle, DEFAULT_SETTINGS.dutyCycle),
  };
}
export function sanitizeSensors(
  value: unknown,
  fallback = DEFAULT_SETTINGS,
): SensorConfig[] | null {
  if (!Array.isArray(value)) return null;
  const result = value
    .map((raw) => {
      const r =
        raw && typeof raw === "object" ? (raw as Record<string, unknown>) : {};
      const key = string(r.key);
      if (!key) return null;
      return {
        key,
        label: string(r.label),
        sensorId: string(r.sensorId),
        sensorToken: string(r.sensorToken),
        runId: string(r.runId),
        captureSessionId: string(r.captureSessionId),
        streamSinceId: Math.max(0, Math.floor(number(r.streamSinceId, 0))),
        settings:
          r.settings && typeof r.settings === "object"
            ? sanitizeSettings(r.settings)
            : { ...fallback },
      };
    })
    .filter((x): x is SensorConfig => x !== null);
  return result.length ? result : null;
}
export function loadPersistedState(): PersistedStateV2 | null {
  if (typeof window === "undefined") return null;
  try {
    const parsed: unknown = JSON.parse(
      window.localStorage.getItem(STORAGE_KEY) ?? "null",
    );
    if (!parsed || typeof parsed !== "object") return null;
    const p = parsed as Record<string, unknown>;
    const fallback =
      p.version === 1 ? sanitizeSettings(p.settings) : DEFAULT_SETTINGS;
    if (p.version !== 1 && p.version !== 2) return null;
    const sensors = sanitizeSensors(p.sensors, fallback) ?? [
      createEmptySensor(),
    ];
    const selected =
      typeof p.selectedSensorKey === "string" &&
      sensors.some((s) => s.key === p.selectedSensorKey)
        ? p.selectedSensorKey
        : sensors[0].key;
    return { version: 2, sensors, selectedSensorKey: selected };
  } catch {
    return null;
  }
}
