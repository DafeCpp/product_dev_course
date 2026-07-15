interface Props {
  active: number;
  total: number;
  running: boolean;
  sent: number;
  accepted: number;
  errors: number;
  rate: string;
  log: string;
  canSend: boolean;
  onStart: () => void;
  onStop: () => void;
  onSend: () => void;
  onReset: () => void;
}

export default function IngestCard(p: Props) {
  return (
    <div className="card">
      <div className="actions">
        <button
          className="btn primary"
          disabled={!p.canSend || p.running}
          onClick={p.onStart}
        >
          Start ingest
        </button>
        <button className="btn danger" disabled={!p.running} onClick={p.onStop}>
          Stop ingest
        </button>
        <button
          className="btn"
          disabled={!p.canSend || p.running}
          onClick={p.onSend}
        >
          Send one batch (all sensors)
        </button>
        <button className="btn" onClick={p.onReset}>
          Reset
        </button>
      </div>
      <div className="kpis">
        <K label="sensors (active/total)" value={`${p.active}/${p.total}`} />
        <K label="generated readings" value={p.sent} />
        <K label="accepted (from API)" value={p.accepted} />
        <K label="errors" value={p.errors} />
        <K label="state" value={p.running ? "running" : "idle"} />
        {p.rate && <K label="effective send rate" value={p.rate} />}
      </div>
      <div className="log">{p.log || "log is empty"}</div>
    </div>
  );
}

function K({ label, value }: { label: string; value: string | number }) {
  return (
    <div className="kpi">
      <div className="label">{label}</div>
      <div className="value">{value}</div>
    </div>
  );
}
