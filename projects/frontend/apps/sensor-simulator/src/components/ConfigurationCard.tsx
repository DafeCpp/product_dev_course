import type {
  PersistedSettings,
  SensorConfig,
  Scenario,
  Waveform,
} from "../domain";
import { sensorDisplayName, sensorIsReady } from "../domain";
interface Props {
  sensors: SensorConfig[];
  selected?: SensorConfig;
  selectedKey: string;
  streamOn: boolean;
  onSelect: (key: string) => void;
  onUpdate: (key: string, patch: Partial<SensorConfig>) => void;
  onUpdateSettings: (key: string, patch: Partial<PersistedSettings>) => void;
  onAdd: () => void;
  onRemove: () => void;
  onCopy: () => void;
}
export default function ConfigurationCard(p: Props) {
  const s = p.selected;
  const set = (patch: Partial<SensorConfig>) => s && p.onUpdate(s.key, patch);
  const setS = (patch: Partial<PersistedSettings>) =>
    s && p.onUpdateSettings(s.key, patch);
  const ss = s?.settings;
  return (
    <div className="card">
      <h2>Ingest (POST /api/v1/telemetry)</h2>
      <div className="hint">
        Параметры сигнала и сценария настраиваются{" "}
        <b>отдельно для каждого датчика</b>. Датчики (включая токены и
        настройки) сохраняются в <span className="badge">localStorage</span>{" "}
        этого браузера.
      </div>
      <div className="sensorTabs" style={{ marginTop: 10 }}>
        {p.sensors.map((x, i) => (
          <button
            key={x.key}
            className={`tab ${x.key === p.selectedKey ? "active" : ""} ${sensorIsReady(x) ? "ok" : "warn"}`}
            onClick={() => p.onSelect(x.key)}
            disabled={p.streamOn}
            title={
              sensorIsReady(x)
                ? `ready — ${x.settings.scenario} / ${x.settings.waveform}`
                : "missing sensor_id/token"
            }
          >
            {sensorDisplayName(x) || `Sensor ${i + 1}`}
          </button>
        ))}
        <button className="tab add" onClick={p.onAdd} disabled={p.streamOn}>
          + Add sensor
        </button>
      </div>
      {s && ss && (
        <>
          <div className="row" style={{ marginTop: 10 }}>
            {field(
              "label (optional)",
              s.label,
              (v) => set({ label: v }),
              "например: motor-temp",
            )}
            {field(
              "sensor_id",
              s.sensorId,
              (v) => set({ sensorId: v }),
              "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
            )}
          </div>
          <div className="row" style={{ marginTop: 10 }}>
            {field(
              "sensor token (Bearer)",
              s.sensorToken,
              (v) => set({ sensorToken: v }),
              "token...",
            )}
            {field(
              "run_id (optional)",
              s.runId,
              (v) => set({ runId: v }),
              "uuid...",
            )}
          </div>
          <div className="row" style={{ marginTop: 10 }}>
            {field(
              "capture_session_id (optional)",
              s.captureSessionId,
              (v) => set({ captureSessionId: v }),
              "uuid...",
            )}
            <div>
              <label>status</label>
              <input
                value={sensorIsReady(s) ? "ready" : "incomplete"}
                readOnly
              />
            </div>
          </div>
          <div className="actions">
            <button
              className="btn danger"
              disabled={p.sensors.length <= 1 || p.streamOn}
              onClick={p.onRemove}
            >
              Remove sensor
            </button>
            <button
              className="btn"
              disabled={p.sensors.length <= 1}
              onClick={p.onCopy}
            >
              Copy settings to all
            </button>
          </div>
          <h3>Signal & scenario settings</h3>
          <div className="row">
            {select(
              "scenario",
              ss.scenario,
              [
                ["steady", "steady stream"],
                ["bursts", "bursts"],
                ["dropout", "dropout"],
                ["out_of_order", "out-of-order"],
                ["late_data", "late data"],
              ],
              (v) => setS({ scenario: v as Scenario }),
            )}
            {select(
              "signal waveform",
              ss.waveform,
              [
                ["sine", "sine"],
                ["pulses", "rect pulses"],
                ["saw", "saw"],
              ],
              (v) => setS({ waveform: v as Waveform }),
            )}
          </div>
          <div className="row" style={{ marginTop: 10 }}>
            {numberField("seed (deterministic)", ss.seed, (v) =>
              setS({ seed: v }),
            )}
            {numberField("amplitude", ss.amplitude, (v) =>
              setS({ amplitude: v }),
            )}
          </div>
          <div className="row" style={{ marginTop: 10 }}>
            {numberField("period (sec)", ss.periodSec, (v) =>
              setS({ periodSec: v }),
            )}
            {numberField("rate (Hz)", ss.rateHz, (v) => setS({ rateHz: v }))}
          </div>
          <div className="row" style={{ marginTop: 10 }}>
            {numberField("batch size (manual)", ss.batchSize, (v) =>
              setS({ batchSize: v }),
            )}
            {numberField(
              "duty cycle (0..1)",
              ss.dutyCycle,
              (v) => setS({ dutyCycle: v }),
              ss.waveform !== "pulses",
            )}
          </div>
        </>
      )}
    </div>
  );
}
function field(
  label: string,
  value: string,
  onChange: (v: string) => void,
  placeholder: string,
) {
  return (
    <div>
      <label>{label}</label>
      <input
        value={value}
        onChange={(e) => onChange(e.target.value)}
        placeholder={placeholder}
      />
    </div>
  );
}
function numberField(
  label: string,
  value: number,
  onChange: (v: number) => void,
  disabled = false,
) {
  return (
    <div>
      <label>{label}</label>
      <input
        type="number"
        value={value}
        onChange={(e) => onChange(Number(e.target.value))}
        disabled={disabled}
      />
    </div>
  );
}
function select(
  label: string,
  value: string,
  options: string[][],
  onChange: (v: string) => void,
) {
  return (
    <div>
      <label>{label}</label>
      <select value={value} onChange={(e) => onChange(e.target.value)}>
        {options.map(([v, text]) => (
          <option key={v} value={v}>
            {text}
          </option>
        ))}
      </select>
    </div>
  );
}
