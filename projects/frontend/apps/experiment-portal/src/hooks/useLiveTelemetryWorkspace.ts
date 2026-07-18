import { useCallback, useEffect, useRef, useState } from "react";
import { generateUUID } from "../utils/uuid";

const RECENT_VALUES_MAX = 30;

export default function useLiveTelemetryWorkspace() {
  const [panelIds, setPanelIds] = useState<string[]>([]);
  const [panelSizes, setPanelSizes] = useState<
    Record<string, { width: number; height: number }>
  >({});
  const [draggingPanelId, setDraggingPanelId] = useState<string | null>(null);
  const [dragOverPanelId, setDragOverPanelId] = useState<string | null>(null);
  const [recentValuesBySensor, setRecentValuesBySensor] = useState<
    Record<string, number[]>
  >({});
  const [panelsWrapWidth, setPanelsWrapWidth] = useState(0);
  const panelsWrapRef = useRef<HTMLDivElement | null>(null);
  const loaded = useRef(false);

  useEffect(() => {
    try {
      const value = JSON.parse(
        window.localStorage.getItem("telemetry_panel_ids") || "[]",
      );
      if (Array.isArray(value))
        setPanelIds(
          value.filter(
            (id): id is string =>
              typeof id === "string" && id.trim().length > 0,
          ),
        );
    } catch {
      // Ignore malformed persisted panels.
    } finally {
      loaded.current = true;
    }
  }, []);
  useEffect(() => {
    if (loaded.current)
      window.localStorage.setItem(
        "telemetry_panel_ids",
        JSON.stringify(panelIds),
      );
  }, [panelIds]);
  useEffect(() => {
    const element = panelsWrapRef.current;
    if (!element || typeof ResizeObserver === "undefined") return;
    const update = () =>
      setPanelsWrapWidth(
        Math.max(0, Math.round(element.getBoundingClientRect().width)),
      );
    update();
    const observer = new ResizeObserver(update);
    observer.observe(element);
    return () => observer.disconnect();
  }, []);

  const addPanel = useCallback(() => {
    setPanelIds((current) => [...current, generateUUID()]);
  }, []);
  const removePanel = useCallback((id: string) => {
    setPanelIds((current) => current.filter((panelId) => panelId !== id));
  }, []);
  const movePanel = useCallback(
    (sourceId: string, targetId: string) =>
      setPanelIds((current) => {
        const from = current.indexOf(sourceId);
        const to = current.indexOf(targetId);
        if (sourceId === targetId || from === -1 || to === -1) return current;
        const next = [...current];
        next.splice(from, 1);
        next.splice(to, 0, sourceId);
        return next;
      }),
    [],
  );
  const onSizeChange = useCallback(
    (id: string, size: { width: number; height: number }) =>
      setPanelSizes((current) => {
        const old = current[id];
        return old?.width === size.width && old.height === size.height
          ? current
          : { ...current, [id]: size };
      }),
    [],
  );
  const onRecordReceived = useCallback(
    (sensorId: string, value: number) =>
      setRecentValuesBySensor((current) => {
        const values = current[sensorId] || [];
        return {
          ...current,
          [sensorId]: [...values.slice(-(RECENT_VALUES_MAX - 1)), value],
        };
      }),
    [],
  );

  return {
    panelIds,
    panelsWrapRef,
    panelsWrapWidth,
    panelSizes,
    draggingPanelId,
    dragOverPanelId,
    recentValuesBySensor,
    addPanel,
    removePanel,
    movePanel,
    onSizeChange,
    onRecordReceived,
    setDraggingPanelId,
    setDragOverPanelId,
    setPanelIds,
  };
}
