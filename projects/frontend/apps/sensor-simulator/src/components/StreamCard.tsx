import type { SensorConfig } from "../domain";
interface Props {
  sensor?: SensorConfig;
  status: string;
  onConnect: () => void;
  onDisconnect: () => void;
  onClear: () => void;
  onSinceId: (id: number) => void;
  events: Record<string, unknown>[];
  streamOn: boolean;
}
export default function StreamCard(p: Props) {
  return (
    <div className="card">
      <h2>SSE stream (GET /api/v1/telemetry/stream)</h2>
      <div className="row">
        {field("sensor", p.sensor?.label || p.sensor?.sensorId || "", true)}
        {field("status", p.status, true)}
      </div>
      <div className="row" style={{ marginTop: 10 }}>
        {
          <div>
            <label>since_id</label>
            <input
              type="number"
              value={p.sensor?.streamSinceId ?? 0}
              onChange={(e) => p.onSinceId(Number(e.target.value))}
            />
          </div>
        }
      </div>
      <div className="actions">
        <button
          className="btn good"
          disabled={!p.sensor || p.streamOn}
          onClick={p.onConnect}
        >
          Connect
        </button>
        <button
          className="btn danger"
          disabled={!p.streamOn}
          onClick={p.onDisconnect}
        >
          Disconnect
        </button>
        <button className="btn" onClick={p.onClear}>
          Clear events
        </button>
      </div>
      <textarea
        readOnly
        value={p.events
          .slice(-50)
          .map((e) => JSON.stringify(e))
          .join("\n")}
        placeholder="events will appear here..."
        style={{ marginTop: 10 }}
      />
    </div>
  );
}
function field(label: string, value: string, readOnly: boolean) {
  return (
    <div>
      <label>{label}</label>
      <input value={value} readOnly={readOnly} />
    </div>
  );
}
