export type Scenario =
  | "steady"
  | "bursts"
  | "dropout"
  | "out_of_order"
  | "late_data";
export type Waveform = "sine" | "pulses" | "saw";

export interface PersistedSettings {
  scenario: Scenario;
  rateHz: number;
  batchSize: number;
  seed: number;
  burstEverySec: number;
  burstDurationSec: number;
  dropoutEverySec: number;
  dropoutDurationSec: number;
  lateSeconds: number;
  outOfOrderFraction: number;
  waveform: Waveform;
  amplitude: number;
  periodSec: number;
  dutyCycle: number;
}
export interface SensorConfig {
  key: string;
  label: string;
  sensorId: string;
  sensorToken: string;
  runId: string;
  captureSessionId: string;
  streamSinceId: number;
  settings: PersistedSettings;
}
export interface PersistedStateV2 {
  version: 2;
  sensors: SensorConfig[];
  selectedSensorKey: string;
}
export interface TelemetryIngestReading {
  timestamp: string;
  raw_value: number;
  physical_value?: number | null;
  meta?: Record<string, unknown>;
}
export interface TelemetryIngestBody {
  sensor_id: string;
  run_id?: string | null;
  capture_session_id?: string | null;
  meta?: Record<string, unknown>;
  readings: TelemetryIngestReading[];
}

export const STORAGE_KEY = "sensor-simulator:params:v1";
export const TELEMETRY_BASE = "/telemetry";
export const DEFAULT_SETTINGS: PersistedSettings = {
  scenario: "steady",
  rateHz: 10,
  batchSize: 50,
  seed: 42,
  burstEverySec: 12,
  burstDurationSec: 3,
  dropoutEverySec: 18,
  dropoutDurationSec: 6,
  lateSeconds: 3600,
  outOfOrderFraction: 0.2,
  waveform: "sine",
  amplitude: 10,
  periodSec: 5,
  dutyCycle: 0.1,
};
export const uuidLike = (value: string) =>
  /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(
    value.trim(),
  );
export const sensorIsReady = (sensor: SensorConfig) =>
  uuidLike(sensor.sensorId) && sensor.sensorToken.trim().length > 0;
export const sensorDisplayName = (sensor: SensorConfig) =>
  sensor.label.trim() ||
  sensor.sensorId.trim().slice(0, 8) ||
  sensor.key.slice(0, 8);
export const clamp = (n: number, min: number, max: number) =>
  Math.max(min, Math.min(max, n));
