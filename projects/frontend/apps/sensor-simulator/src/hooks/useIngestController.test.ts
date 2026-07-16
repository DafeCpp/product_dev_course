import { act, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { DEFAULT_SETTINGS, type SensorConfig } from "../domain";
import { useIngestController } from "./useIngestController";

function makeSensor(settings = DEFAULT_SETTINGS): SensorConfig {
  return {
    key: "sensor_test",
    label: "test-sensor",
    sensorId: "11111111-1111-1111-1111-111111111111",
    sensorToken: "token",
    runId: "",
    captureSessionId: "",
    streamSinceId: 0,
    settings: { ...settings },
  };
}

describe("useIngestController", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date("2026-01-01T00:00:00Z"));
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue(
        new Response(JSON.stringify({ accepted: 1 }), { status: 200 }),
      ),
    );
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.unstubAllGlobals();
  });

  it("stops ingest when no ready sensors remain", async () => {
    const sensor = makeSensor();
    const { result, rerender } = renderHook(
      ({ sensors }) => useIngestController(sensors),
      { initialProps: { sensors: [sensor] } },
    );

    act(() => result.current.start());
    await act(async () => vi.advanceTimersByTimeAsync(0));

    rerender({ sensors: [{ ...sensor, sensorToken: "" }] });
    await act(async () => vi.advanceTimersByTimeAsync(1000));

    expect(result.current.isRunning).toBe(false);
    expect(result.current.log).toMatch(/no active sensors.*stopping/i);
  });

  it("uses a clamped rate for continuous ingest scheduling and payloads", async () => {
    const sensor = makeSensor({ ...DEFAULT_SETTINGS, rateHz: 0 });
    const fetchMock = vi.mocked(fetch);
    const { result } = renderHook(() => useIngestController([sensor]));

    act(() => result.current.start());
    await act(async () => vi.advanceTimersByTimeAsync(1000));

    expect(fetchMock).toHaveBeenCalled();
    const [, init] = fetchMock.mock.calls.at(-1)!;
    const body = JSON.parse(init?.body as string) as {
      meta: { rate_hz: number };
    };

    expect(body.meta.rate_hz).toBe(1);
  });
});
