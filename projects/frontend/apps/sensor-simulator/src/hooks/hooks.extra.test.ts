import { act, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { DEFAULT_SETTINGS, STORAGE_KEY, type SensorConfig } from "../domain";
import { openTelemetryStream } from "../api";
import { useIngestController } from "./useIngestController";
import { usePersistedSensors } from "./usePersistedSensors";
import { useSensorStream } from "./useSensorStream";

function makeSensor(overrides: Partial<SensorConfig> = {}): SensorConfig {
  return {
    key: "sensor_test",
    label: "test-sensor",
    sensorId: "11111111-1111-1111-1111-111111111111",
    sensorToken: "token",
    runId: "",
    captureSessionId: "",
    streamSinceId: 0,
    settings: { ...DEFAULT_SETTINGS },
    ...overrides,
  };
}

describe("openTelemetryStream", () => {
  afterEach(() => vi.unstubAllGlobals());

  it("falls back to the status text when the error body is empty", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue(
        new Response("", { status: 503, statusText: "Service Unavailable" }),
      ),
    );

    await expect(
      openTelemetryStream("id", "token", 0, new AbortController().signal),
    ).rejects.toThrow("HTTP 503: Service Unavailable");
  });

  it("only sends since_id when resuming", async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response("", { status: 200 }));
    vi.stubGlobal("fetch", fetchMock);
    const signal = new AbortController().signal;

    await openTelemetryStream("id", "token", 0, signal);
    await openTelemetryStream("id", "token", 7, signal);

    const [first, second] = fetchMock.mock.calls.map(([url]) => new URL(String(url)));
    expect(first.searchParams.has("since_id")).toBe(false);
    expect(second.searchParams.get("since_id")).toBe("7");
  });
});

describe("useSensorStream", () => {
  afterEach(() => vi.unstubAllGlobals());

  it("does nothing when there is no selected sensor", () => {
    const onLog = vi.fn();
    const { result } = renderHook(() => useSensorStream(undefined, onLog, vi.fn()));

    act(() => result.current.connect());

    expect(onLog).not.toHaveBeenCalled();
    expect(result.current.streamOn).toBe(false);
    expect(result.current.status).toBe("idle");
  });

  it("logs connect and disconnect for the selected sensor", () => {
    vi.stubGlobal("fetch", vi.fn().mockReturnValue(new Promise(() => {})));
    const onLog = vi.fn();
    const { result } = renderHook(() =>
      useSensorStream(makeSensor({ label: "", streamSinceId: 3 }), onLog, vi.fn()),
    );

    act(() => result.current.connect());
    expect(result.current.streamOn).toBe(true);
    expect(onLog).toHaveBeenLastCalledWith(
      expect.stringMatching(/stream connect sensor=11111111-.* since_id=3/),
    );

    act(() => result.current.disconnect());
    expect(result.current.streamOn).toBe(false);
    expect(onLog).toHaveBeenLastCalledWith(expect.stringMatching(/stream disconnect/));
  });
});

describe("usePersistedSensors", () => {
  beforeEach(() => {
    localStorage.clear();
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.restoreAllMocks();
  });

  it("keeps working when localStorage rejects writes", async () => {
    vi.spyOn(Storage.prototype, "setItem").mockImplementation(() => {
      throw new Error("quota exceeded");
    });
    const { result } = renderHook(() => usePersistedSensors());

    await act(async () => vi.advanceTimersByTimeAsync(600));

    expect(result.current.sensors).toHaveLength(1);
  });

  it("recreates a sensor after the last one is removed", async () => {
    const onLog = vi.fn();
    const { result } = renderHook(() => usePersistedSensors(onLog));
    const [only] = result.current.sensors;

    act(() => result.current.removeSensor(only.key));

    expect(result.current.sensors).toHaveLength(1);
    expect(result.current.sensors[0].key).not.toBe(only.key);
    expect(result.current.selectedSensorKey).toBe(result.current.sensors[0].key);
    expect(onLog).toHaveBeenCalledWith(expect.stringMatching(/remove sensor/));
  });

  it("moves the selection to the first sensor when the selected one disappears", () => {
    localStorage.setItem(
      STORAGE_KEY,
      JSON.stringify({
        version: 2,
        sensors: [{ key: "a" }, { key: "b" }],
        selectedSensorKey: "b",
      }),
    );
    const { result } = renderHook(() => usePersistedSensors());

    act(() => result.current.removeSensor("b"));

    expect(result.current.selectedSensorKey).toBe("a");
    expect(result.current.selectedSensor.key).toBe("a");
  });

  it("updates one sensor without touching the others", () => {
    localStorage.setItem(
      STORAGE_KEY,
      JSON.stringify({
        version: 2,
        sensors: [{ key: "a" }, { key: "b" }],
        selectedSensorKey: "a",
      }),
    );
    const { result } = renderHook(() => usePersistedSensors());

    act(() => result.current.updateSensor("b", { label: "renamed" }));
    act(() => result.current.updateSensorSettings("b", { amplitude: 99 }));

    const [a, b] = result.current.sensors;
    expect(a.label).toBe("");
    expect(a.settings.amplitude).toBe(DEFAULT_SETTINGS.amplitude);
    expect(b.label).toBe("renamed");
    expect(b.settings.amplitude).toBe(99);
  });
});

describe("useIngestController edge cases", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date("2026-01-01T00:00:00Z"));
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue(new Response("{}", { status: 200 })),
    );
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.unstubAllGlobals();
  });

  it("does not start without a ready sensor", () => {
    const { result } = renderHook(() =>
      useIngestController([makeSensor({ sensorToken: "" })]),
    );

    act(() => result.current.start());

    expect(result.current.isRunning).toBe(false);
    expect(result.current.log).toBe("");
  });

  it("logs non-Error failures of the transport", async () => {
    vi.stubGlobal("fetch", vi.fn().mockRejectedValue("socket closed"));
    const { result } = renderHook(() => useIngestController([makeSensor()]));

    act(() => result.current.sendOneBatch());
    await act(async () => vi.advanceTimersByTimeAsync(0));

    expect(result.current.errors).toBe(1);
    expect(result.current.log).toMatch(/network error sensor=test-sensor: socket closed/);
  });

  it("does not send while a dropout scenario pauses the sensor", async () => {
    const paused = makeSensor({
      settings: {
        ...DEFAULT_SETTINGS,
        scenario: "dropout",
        dropoutEverySec: 0,
        dropoutDurationSec: 60,
      },
    });
    const { result } = renderHook(() => useIngestController([paused]));

    act(() => result.current.start());
    await act(async () => vi.advanceTimersByTimeAsync(2500));

    expect(fetch).not.toHaveBeenCalled();
    expect(result.current.isRunning).toBe(true);
    act(() => result.current.stop());
  });

  it("cancels the scheduled tick when the component unmounts", async () => {
    const { result, unmount } = renderHook(() => useIngestController([makeSensor()]));

    act(() => result.current.start());
    await act(async () => vi.advanceTimersByTimeAsync(0));
    const callsBeforeUnmount = vi.mocked(fetch).mock.calls.length;
    unmount();
    await vi.advanceTimersByTimeAsync(5000);

    expect(vi.mocked(fetch).mock.calls.length).toBe(callsBeforeUnmount);
  });

  it("stop is a no-op for the timer when nothing is scheduled", () => {
    const { result } = renderHook(() => useIngestController([makeSensor()]));

    act(() => result.current.stop());

    expect(result.current.log).toMatch(/⏹ stop/);
  });
});
