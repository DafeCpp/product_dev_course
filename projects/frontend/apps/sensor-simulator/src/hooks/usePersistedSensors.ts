import { useCallback, useEffect, useMemo, useState } from "react";
import {
  DEFAULT_SETTINGS,
  type PersistedSettings,
  type SensorConfig,
} from "../domain";
import { createEmptySensor, loadPersistedState } from "../storage";
export function usePersistedSensors(onLog?: (line: string) => void) {
  const initial = useMemo(
    () =>
      loadPersistedState() ??
      (() => {
        const s = createEmptySensor();
        return { version: 2 as const, sensors: [s], selectedSensorKey: s.key };
      })(),
    [],
  );
  const [sensors, setSensors] = useState<SensorConfig[]>(initial.sensors);
  const [selectedSensorKey, setSelectedSensorKey] = useState(
    initial.selectedSensorKey,
  );
  const selectedSensor = useMemo(
    () => sensors.find((s) => s.key === selectedSensorKey) ?? sensors[0],
    [sensors, selectedSensorKey],
  );
  useEffect(() => {
    if (!sensors.length) {
      const s = createEmptySensor();
      setSensors([s]);
      setSelectedSensorKey(s.key);
    } else if (!sensors.some((s) => s.key === selectedSensorKey))
      setSelectedSensorKey(sensors[0].key);
  }, [sensors, selectedSensorKey]);
  useEffect(() => {
    const timer = window.setTimeout(() => {
      try {
        localStorage.setItem(
          "sensor-simulator:params:v1",
          JSON.stringify({ version: 2, sensors, selectedSensorKey }),
        );
      } catch {
        /* storage is optional */
      }
    }, 500);
    return () => window.clearTimeout(timer);
  }, [sensors, selectedSensorKey]);
  const updateSensor = useCallback(
    (key: string, patch: Partial<SensorConfig>) =>
      setSensors((prev) =>
        prev.map((s) => (s.key === key ? { ...s, ...patch } : s)),
      ),
    [],
  );
  const updateSensorSettings = useCallback(
    (key: string, patch: Partial<PersistedSettings>) =>
      setSensors((prev) =>
        prev.map((s) =>
          s.key === key ? { ...s, settings: { ...s.settings, ...patch } } : s,
        ),
      ),
    [],
  );
  const addSensor = useCallback(() => {
    const s = createEmptySensor();
    if (selectedSensor) s.settings = { ...selectedSensor.settings };
    setSensors((prev) => [...prev, s]);
    setSelectedSensorKey(s.key);
    onLog?.(`[${new Date().toISOString()}] ➕ add sensor`);
  }, [onLog, selectedSensor]);
  const removeSensor = useCallback(
    (key: string) => {
      setSensors((prev) => prev.filter((s) => s.key !== key));
      onLog?.(`[${new Date().toISOString()}] ➖ remove sensor`);
    },
    [onLog],
  );
  const copySettingsToAll = useCallback(() => {
    if (selectedSensor)
      setSensors((prev) =>
        prev.map((s) => ({ ...s, settings: { ...selectedSensor.settings } })),
      );
  }, [selectedSensor]);
  return {
    sensors,
    selectedSensor,
    selectedSensorKey,
    setSelectedSensorKey,
    updateSensor,
    updateSensorSettings,
    addSensor,
    removeSensor,
    copySettingsToAll,
    DEFAULT_SETTINGS,
  };
}
