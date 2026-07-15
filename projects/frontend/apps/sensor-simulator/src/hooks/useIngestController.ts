import { useCallback, useEffect, useRef, useState } from "react";
import { postTelemetry } from "../api";
import {
  clamp,
  sensorDisplayName,
  sensorIsReady,
  type SensorConfig,
} from "../domain";
import {
  assemblePayload,
  buildReadings,
  scenarioEffectiveRate,
  scenarioIsPausedAt,
  type GeneratorRuntime,
} from "../generator";
export function useIngestController(
  sensors: SensorConfig[],
  onLogExternal?: (line: string) => void,
) {
  const [isRunning, setIsRunning] = useState(false);
  const [sent, setSent] = useState(0);
  const [accepted, setAccepted] = useState(0);
  const [errors, setErrors] = useState(0);
  const [lastHttpStatus, setLastHttpStatus] = useState<number | null>(null);
  const [log, setLog] = useState("");
  const [effectiveRateDisplay, setEffectiveRateDisplay] = useState("");
  const timer = useRef<number | null>(null);
  const running = useRef(false);
  const latest = useRef(sensors);
  latest.current = sensors;
  const runtimes = useRef(new Map<string, GeneratorRuntime>());
  const lastSend = useRef(new Map<string, number>());
  const appendLog = useCallback(
    (line: string) => {
      setLog((prev) =>
        `${prev}${prev ? "\n" : ""}${line}`.split("\n").slice(-300).join("\n"),
      );
      onLogExternal?.(line);
    },
    [onLogExternal],
  );
  const sendBatch = useCallback(
    async (
      sensor: SensorConfig,
      n: number,
      rate: number,
      continuous: boolean,
      startMs?: number,
    ) => {
      if (!sensorIsReady(sensor)) return;
      const runtime = runtimes.current.get(sensor.key) ?? {
        sequence: 0,
        lastTimestampMs: 0,
        rngState: 0,
        seed: sensor.settings.seed,
      };
      const readings = buildReadings(
        sensor.key,
        n,
        rate,
        sensor.settings,
        runtime,
        continuous,
        startMs,
      );
      runtimes.current.set(sensor.key, runtime);
      const body = assemblePayload(sensor, n, rate, sensor.settings, readings);
      appendLog(
        `[${new Date().toISOString()}] POST /api/v1/telemetry sensor=${sensorDisplayName(sensor)} readings=${n}`,
      );
      try {
        const res = await postTelemetry(body, sensor.sensorToken.trim());
        setLastHttpStatus(res.status);
        if (res.ok) {
          setSent((v) => v + n);
          const data = JSON.parse(res.text) as { accepted?: number };
          setAccepted(
            (v) => v + (typeof data.accepted === "number" ? data.accepted : n),
          );
          appendLog(
            `[${new Date().toISOString()}] ✅ ${res.status} sensor=${sensorDisplayName(sensor)}: ${res.text}`,
          );
        } else {
          setErrors((v) => v + 1);
          appendLog(
            `[${new Date().toISOString()}] ❌ ${res.status} sensor=${sensorDisplayName(sensor)}: ${res.text}`,
          );
        }
      } catch (e) {
        setErrors((v) => v + 1);
        appendLog(
          `[${new Date().toISOString()}] ❌ network error sensor=${sensorDisplayName(sensor)}: ${e instanceof Error ? e.message : String(e)}`,
        );
      }
    },
    [appendLog],
  );
  const sendOneBatch = useCallback(() => {
    void Promise.all(
      latest.current
        .filter(sensorIsReady)
        .map((s) =>
          sendBatch(
            s,
            clamp(s.settings.batchSize, 1, 10000),
            clamp(s.settings.rateHz, 1, 10000),
            false,
          ),
        ),
    );
  }, [sendBatch]);
  const stop = useCallback(() => {
    running.current = false;
    setIsRunning(false);
    if (timer.current !== null) {
      clearTimeout(timer.current);
      timer.current = null;
    }
    appendLog(`[${new Date().toISOString()}] ⏹ stop`);
  }, [appendLog]);
  const start = useCallback(() => {
    const startMs = Date.now();
    if (!latest.current.some(sensorIsReady)) return;
    running.current = true;
    setIsRunning(true);
    appendLog(
      `[${new Date().toISOString()}] ▶️ start sensors=${latest.current.filter(sensorIsReady).length}`,
    );
    const tick = () => {
      const elapsed = (Date.now() - startMs) / 1000;
      let interval = 1000;
      const rates: string[] = [];
      for (const sensor of latest.current.filter(sensorIsReady)) {
        if (scenarioIsPausedAt(sensor.settings, elapsed)) continue;
        const rate = scenarioEffectiveRate(sensor.settings, elapsed);
        interval = Math.min(interval, 1000 / clamp(rate, 1, 10000));
        rates.push(`${sensorDisplayName(sensor)}: ${rate.toFixed(1)} Hz`);
        const last = lastSend.current.get(sensor.key) ?? 0;
        if (Date.now() - last >= (1000 / rate) * 0.8) {
          const count = Math.min(
            Math.max(1, Math.round((Date.now() - last) / (1000 / rate))),
            Math.max(10, Math.round(rate * 2)),
          );
          void sendBatch(sensor, count, rate, true, last || startMs);
          lastSend.current.set(sensor.key, Date.now());
        }
      }
      setEffectiveRateDisplay(rates.join(" | "));
      if (running.current)
        timer.current = window.setTimeout(tick, Math.max(10, interval));
    };
    timer.current = window.setTimeout(tick, 0);
  }, [appendLog, sendBatch]);
  const reset = useCallback(() => {
    setSent(0);
    setAccepted(0);
    setErrors(0);
    setLastHttpStatus(null);
    setLog("");
    runtimes.current.clear();
    lastSend.current.clear();
    appendLog(`[${new Date().toISOString()}] 🧹 reset counters/log`);
  }, [appendLog]);
  useEffect(
    () => () => {
      running.current = false;
      if (timer.current !== null) clearTimeout(timer.current);
    },
    [],
  );
  return {
    isRunning,
    sent,
    accepted,
    errors,
    lastHttpStatus,
    log,
    effectiveRateDisplay,
    appendLog,
    start,
    stop,
    sendOneBatch,
    reset,
  };
}
