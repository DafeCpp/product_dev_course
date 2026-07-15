import { useCallback, useState } from "react";
import { useTelemetryStream } from "../../../../common/src/hooks/useTelemetryStream";
import type { TelemetryStreamRecord } from "../../../../common/src/types/telemetry";
import { openTelemetryStream } from "../api";
import type { SensorConfig } from "../domain";
export function useSensorStream(
  sensor: SensorConfig | undefined,
  onLog: (line: string) => void,
  updateSensor: (key: string, patch: Partial<SensorConfig>) => void,
) {
  const [activeKey, setActiveKey] = useState<string | null>(null);
  const id = sensor?.sensorId.trim() ?? "";
  const token = sensor?.sensorToken.trim() ?? "";
  const key = sensor?.key;
  const stream = useTelemetryStream(id, {
    open: ({ sinceId, signal }) =>
      openTelemetryStream(id, token, sinceId ?? 0, signal).then((response) => ({
        response,
      })),
    initialSinceId: sensor?.streamSinceId ?? 0,
    autoReconnect: false,
    onRecord: (record: TelemetryStreamRecord) => {
      if (key) updateSensor(key, { streamSinceId: record.id });
    },
    onError: (error) =>
      onLog(`[${new Date().toISOString()}] ⚠️ stream error: ${error.message}`),
  });
  const connect = useCallback(() => {
    if (!sensor) return;
    setActiveKey(sensor.key);
    onLog(
      `[${new Date().toISOString()}] 🔌 stream connect sensor=${sensor.label || sensor.sensorId} since_id=${sensor.streamSinceId}`,
    );
    stream.start({ sinceId: sensor.streamSinceId });
  }, [onLog, sensor, stream]);
  const disconnect = useCallback(() => {
    stream.stop();
    setActiveKey(null);
    onLog(`[${new Date().toISOString()}] 🔌 stream disconnect`);
  }, [onLog, stream]);
  return {
    ...stream,
    connect,
    disconnect,
    streamOn:
      activeKey !== null &&
      stream.status !== "stopped" &&
      stream.status !== "idle",
  };
}
