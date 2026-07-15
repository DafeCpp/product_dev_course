import { useEffect, useMemo, useState } from "react";
import { useTelemetryQuery } from "frontend-common";
import { telemetryApi } from "../api/client";
import type {
  Sensor,
  TelemetryAggregatedRecord,
  TelemetryQueryRecord,
} from "../types";

export type HistoryValueMode = "physical" | "raw";
type HistoryOrder = "asc" | "desc";
interface StoredHistoryState {
  captureSessionId: string;
  sensorIds: string[];
  valueMode: HistoryValueMode;
  includeLate: boolean;
  maxPoints: number;
  order: HistoryOrder;
}
const MAX_SENSORS = 50;
const MAX_POINTS = 20000;
const DEFAULT_POINTS = 5000;

export default function useTelemetryHistory(sensors: Sensor[]) {
  const [captureSessionId, setCaptureSessionId] = useState("");
  const [sensorIds, setSensorIds] = useState<string[]>([]);
  const [valueMode, setValueMode] = useState<HistoryValueMode>("physical");
  const [includeLate, setIncludeLate] = useState(true);
  const [maxPoints, setMaxPoints] = useState(DEFAULT_POINTS);
  const [order, setOrder] = useState<HistoryOrder>("asc");
  const [useAggregated, setUseAggregated] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [filter, setFilter] = useState("");
  const [loaded, setLoaded] = useState(false);
  useEffect(() => {
    try {
      const state = JSON.parse(
        window.localStorage.getItem("telemetry_history_state") || "{}",
      ) as Partial<StoredHistoryState>;
      if (typeof state.captureSessionId === "string")
        setCaptureSessionId(state.captureSessionId);
      if (Array.isArray(state.sensorIds))
        setSensorIds(
          state.sensorIds
            .filter((id): id is string => typeof id === "string")
            .slice(0, MAX_SENSORS),
        );
      if (state.valueMode === "physical" || state.valueMode === "raw")
        setValueMode(state.valueMode);
      if (typeof state.includeLate === "boolean")
        setIncludeLate(state.includeLate);
      if (
        typeof state.maxPoints === "number" &&
        Number.isFinite(state.maxPoints)
      )
        setMaxPoints(
          Math.max(100, Math.min(MAX_POINTS, Math.round(state.maxPoints))),
        );
      if (state.order === "asc" || state.order === "desc")
        setOrder(state.order);
    } catch {
      /* Ignore malformed persisted state. */
    } finally {
      setLoaded(true);
    }
  }, []);
  useEffect(() => {
    if (loaded)
      window.localStorage.setItem(
        "telemetry_history_state",
        JSON.stringify({
          captureSessionId,
          sensorIds,
          valueMode,
          includeLate,
          maxPoints,
          order,
        }),
      );
  }, [
    captureSessionId,
    includeLate,
    loaded,
    maxPoints,
    order,
    sensorIds,
    valueMode,
  ]);
  const displayMaxPoints = Math.min(
    MAX_POINTS,
    Number.isFinite(maxPoints) && maxPoints > 0 ? maxPoints : DEFAULT_POINTS,
  );
  const query = useTelemetryQuery({
    ...(useAggregated
      ? {
          mode: "aggregated" as const,
          captureSessionId,
          sensorIds: sensorIds.length ? sensorIds : undefined,
          order,
          limit: displayMaxPoints,
          hardCapLimit: MAX_POINTS,
        }
      : {
          mode: "raw" as const,
          captureSessionId,
          sensorIds: sensorIds.length ? sensorIds : undefined,
          includeLate,
          order,
          maxPoints: displayMaxPoints,
          pageSize: 2000,
          hardCapLimit: MAX_POINTS,
        }),
    enabled: false,
    query: telemetryApi.query,
    aggregated: telemetryApi.aggregated,
  });
  const points = query.mode === "raw" ? query.points : [];
  const buckets = query.mode === "aggregated" ? query.buckets : [];
  const sensorById = useMemo(
    () => new Map(sensors.map((sensor) => [sensor.id, sensor])),
    [sensors],
  );
  const availableSensors = useMemo(
    () => sensors.filter((sensor) => !sensorIds.includes(sensor.id)),
    [sensorIds, sensors],
  );
  const filteredSensors = useMemo(() => {
    const value = filter.trim().toLowerCase();
    return value
      ? availableSensors.filter((sensor) =>
          `${sensor.name} ${sensor.type} ${sensor.id}`
            .toLowerCase()
            .includes(value),
        )
      : availableSensors;
  }, [availableSensors, filter]);
  const rawSeries = useMemo(
    () => groupedPoints(points, sensorById, valueMode),
    [points, sensorById, valueMode],
  );
  const aggregatedSeries = useMemo(
    () => groupedBuckets(buckets, sensorById, valueMode),
    [buckets, sensorById, valueMode],
  );
  const effectiveSensorIds = useMemo(
    () =>
      sensorIds.length
        ? sensorIds
        : Array.from(new Set(points.map((point) => point.sensor_id))),
    [points, sensorIds],
  );
  const lastTimestamp = useMemo(
    () =>
      points.reduce<string | null>(
        (latest, point) =>
          !latest || Date.parse(point.timestamp) > Date.parse(latest)
            ? point.timestamp
            : latest,
        null,
      ),
    [points],
  );
  const cursorBySensor = useMemo(
    () =>
      points.reduce<Record<string, { timestamp: string; id: number }>>(
        (result, point) => {
          const prior = result[point.sensor_id];
          if (
            !prior ||
            Date.parse(point.timestamp) > Date.parse(prior.timestamp) ||
            (point.timestamp === prior.timestamp && point.id > prior.id)
          )
            result[point.sensor_id] = {
              timestamp: point.timestamp,
              id: point.id,
            };
          return result;
        },
        {},
      ),
    [points],
  );
  const add = (id: string) =>
    setSensorIds((current) => {
      if (!id || current.includes(id)) return current;
      if (current.length >= MAX_SENSORS) {
        setError("Можно выбрать не более 50 сенсоров");
        return current;
      }
      return [...current, id];
    });
  const addAll = () =>
    setSensorIds((current) =>
      [...current, ...availableSensors.map((sensor) => sensor.id)].slice(
        0,
        MAX_SENSORS,
      ),
    );
  const load = () => {
    if (!captureSessionId) return;
    setError(null);
    void query.refetch();
  };
  return {
    captureSessionId,
    setCaptureSessionId,
    sensorIds,
    valueMode,
    setValueMode,
    includeLate,
    setIncludeLate,
    maxPoints,
    setMaxPoints,
    order,
    setOrder,
    useAggregated,
    setUseAggregated,
    error,
    filter,
    setFilter,
    availableSensors,
    filteredSensors,
    sensorById,
    rawSeries,
    aggregatedSeries,
    effectiveSensorIds,
    lastTimestamp,
    cursorBySensor,
    displayMaxPoints,
    loading: query.isFetching,
    loadedCount: query.loadedCount,
    loadError: query.error?.message || null,
    wasTruncated: query.mode === "raw" && query.wasTruncated,
    hasData: useAggregated
      ? aggregatedSeries.some((series) => series.avg.length)
      : rawSeries.some((series) => series.y.length),
    add,
    addAll,
    remove: (id: string) =>
      setSensorIds((current) => current.filter((value) => value !== id)),
    clear: () => setSensorIds([]),
    load,
  };
}

function groupedPoints(
  points: TelemetryQueryRecord[],
  sensors: Map<string, Sensor>,
  valueMode: HistoryValueMode,
) {
  const groups = new Map<string, TelemetryQueryRecord[]>();
  points.forEach((point) =>
    groups.set(point.sensor_id, [
      ...(groups.get(point.sensor_id) || []),
      point,
    ]),
  );
  return Array.from(groups).map(([id, values]) => {
    const ordered = values.slice().sort((left, right) => left.id - right.id);
    return {
      name: sensors.get(id)?.name || id,
      x: ordered.map((point) => point.timestamp),
      y: ordered.map((point) =>
        valueMode === "physical" ? point.physical_value : point.raw_value,
      ),
    };
  });
}
function groupedBuckets(
  buckets: TelemetryAggregatedRecord[],
  sensors: Map<string, Sensor>,
  valueMode: HistoryValueMode,
) {
  const groups = new Map<string, TelemetryAggregatedRecord[]>();
  buckets.forEach((bucket) => {
    const key = `${bucket.sensor_id || ""}::${bucket.signal || ""}`;
    groups.set(key, [...(groups.get(key) || []), bucket]);
  });
  return Array.from(groups).map(([key, values]) => {
    const ordered = values
      .slice()
      .sort(
        (left, right) => Date.parse(left.bucket) - Date.parse(right.bucket),
      );
    const [sensorId, signal] = key.split("::");
    const label = `${sensors.get(sensorId)?.name || sensorId}${signal ? ` / ${signal}` : ""}`;
    return {
      name: label,
      x: ordered.map((value) => value.bucket),
      avg: ordered.map((value) =>
        valueMode === "physical" ? value.avg_physical : value.avg_raw,
      ),
      min: ordered.map((value) =>
        valueMode === "physical" ? value.min_physical : value.min_raw,
      ),
      max: ordered.map((value) =>
        valueMode === "physical" ? value.max_physical : value.max_raw,
      ),
    };
  });
}
