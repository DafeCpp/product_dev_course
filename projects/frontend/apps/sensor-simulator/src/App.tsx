import { useCallback, useMemo, useState } from "react";
import ConfigurationCard from "./components/ConfigurationCard";
import IngestCard from "./components/IngestCard";
import StreamCard from "./components/StreamCard";
import { sensorIsReady } from "./domain";
import { useIngestController } from "./hooks/useIngestController";
import { usePersistedSensors } from "./hooks/usePersistedSensors";
import { useSensorStream } from "./hooks/useSensorStream";

export function App() {
  const [externalLog, setExternalLog] = useState<string[]>([]);
  const appendExternalLog = useCallback(
    (line: string) => setExternalLog((prev) => [...prev, line].slice(-300)),
    [],
  );
  const sensorsState = usePersistedSensors(appendExternalLog);
  const ingest = useIngestController(sensorsState.sensors, appendExternalLog);
  const stream = useSensorStream(
    sensorsState.selectedSensor,
    appendExternalLog,
    sensorsState.updateSensor,
  );
  const events = stream.points as unknown as Record<string, unknown>[];
  const active = useMemo(
    () => sensorsState.sensors.filter(sensorIsReady).length,
    [sensorsState.sensors],
  );
  const log = [...externalLog, ingest.log]
    .filter(Boolean)
    .join("\n")
    .split("\n")
    .slice(-300)
    .join("\n");
  const statusClass =
    ingest.lastHttpStatus === null
      ? "pill"
      : ingest.lastHttpStatus < 300
        ? "pill ok"
        : "pill bad";

  return (
    <div className="container">
      <div className="header">
        <div className="title">
          <h1>Sensor Simulator</h1>
          <p>
            Симулирует телеметрию и отправляет батчи в{" "}
            <span className="badge">/telemetry → telemetry-ingest-service</span>
            . Для ingest нужен{" "}
            <span className="badge">
              Authorization: Bearer &lt;sensor token&gt;
            </span>
            .
          </p>
        </div>
        <div className={statusClass} title="Last ingest HTTP status">
          <span className="dot" />
          <span>HTTP {ingest.lastHttpStatus ?? "—"}</span>
        </div>
      </div>
      <div className="grid">
        <ConfigurationCard
          sensors={sensorsState.sensors}
          selected={sensorsState.selectedSensor}
          selectedKey={sensorsState.selectedSensorKey}
          streamOn={stream.streamOn}
          onSelect={sensorsState.setSelectedSensorKey}
          onUpdate={sensorsState.updateSensor}
          onUpdateSettings={sensorsState.updateSensorSettings}
          onAdd={sensorsState.addSensor}
          onRemove={() =>
            sensorsState.selectedSensor &&
            sensorsState.removeSensor(sensorsState.selectedSensor.key)
          }
          onCopy={sensorsState.copySettingsToAll}
        />
        <IngestCard
          active={active}
          total={sensorsState.sensors.length}
          running={ingest.isRunning}
          sent={ingest.sent}
          accepted={ingest.accepted}
          errors={ingest.errors}
          rate={ingest.effectiveRateDisplay}
          log={log}
          canSend={active > 0}
          onStart={ingest.start}
          onStop={ingest.stop}
          onSend={ingest.sendOneBatch}
          onReset={ingest.reset}
        />
        <StreamCard
          sensor={sensorsState.selectedSensor}
          status={stream.status}
          streamOn={stream.streamOn}
          events={events}
          onConnect={stream.connect}
          onDisconnect={stream.disconnect}
          onClear={stream.clear}
          onSinceId={(id) =>
            sensorsState.selectedSensor &&
            sensorsState.updateSensor(sensorsState.selectedSensor.key, {
              streamSinceId: id,
            })
          }
        />
      </div>
    </div>
  );
}
