import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { DEFAULT_SETTINGS, STORAGE_KEY } from "./domain";
import {
  createEmptySensor,
  loadPersistedState,
  sanitizeSensors,
  sanitizeSettings,
} from "./storage";

describe("sanitizeSettings", () => {
  it.each([undefined, null, "text", 42])(
    "uses the defaults for a non-object input (%s)",
    (raw) => {
      expect(sanitizeSettings(raw)).toEqual(DEFAULT_SETTINGS);
    },
  );

  it("replaces unknown enum values and non-finite numbers with the defaults", () => {
    const settings = sanitizeSettings({
      scenario: "chaos",
      waveform: "square",
      rateHz: Number.NaN,
      amplitude: "loud",
      seed: Infinity,
    });

    expect(settings).toEqual(DEFAULT_SETTINGS);
  });

  it("keeps valid values", () => {
    const settings = sanitizeSettings({
      scenario: "late_data",
      waveform: "saw",
      rateHz: 5,
      lateSeconds: 60,
    });

    expect(settings).toMatchObject({
      scenario: "late_data",
      waveform: "saw",
      rateHz: 5,
      lateSeconds: 60,
      batchSize: DEFAULT_SETTINGS.batchSize,
    });
  });
});

describe("sanitizeSensors", () => {
  it.each([undefined, null, {}, "sensors"])("rejects a non-array (%s)", (value) => {
    expect(sanitizeSensors(value)).toBeNull();
  });

  it("returns null when no entry has a key", () => {
    expect(sanitizeSensors([{ label: "x" }, null, 5])).toBeNull();
  });

  it("applies the fallback settings and normalizes the stream cursor", () => {
    const fallback = { ...DEFAULT_SETTINGS, seed: 99 };

    const [first, second] = sanitizeSensors(
      [
        { key: "a", streamSinceId: -4, label: 7 },
        { key: "b", streamSinceId: 12.9, settings: { seed: 1 } },
      ],
      fallback,
    )!;

    expect(first.settings.seed).toBe(99);
    expect(first.streamSinceId).toBe(0);
    expect(first.label).toBe("");
    expect(second.streamSinceId).toBe(12);
    expect(second.settings.seed).toBe(1);
  });
});

describe("loadPersistedState", () => {
  beforeEach(() => localStorage.clear());

  const store = (value: unknown) =>
    localStorage.setItem(STORAGE_KEY, JSON.stringify(value));

  it("returns null when nothing is stored", () => {
    expect(loadPersistedState()).toBeNull();
  });

  it.each([null, "text", 5, { version: 3 }, { hello: "world" }])(
    "returns null for an unsupported payload (%j)",
    (payload) => {
      store(payload);
      expect(loadPersistedState()).toBeNull();
    },
  );

  it("keeps the stored selection when it exists", () => {
    store({
      version: 2,
      sensors: [{ key: "a" }, { key: "b" }],
      selectedSensorKey: "b",
    });

    expect(loadPersistedState()?.selectedSensorKey).toBe("b");
  });

  it.each([undefined, "missing", 7])(
    "selects the first sensor when the stored selection is invalid (%s)",
    (selected) => {
      store({
        version: 2,
        sensors: [{ key: "a" }, { key: "b" }],
        selectedSensorKey: selected,
      });

      expect(loadPersistedState()?.selectedSensorKey).toBe("a");
    },
  );
});

describe("createEmptySensor", () => {
  afterEach(() => vi.unstubAllGlobals());

  it("builds a unique default sensor", () => {
    const a = createEmptySensor();
    const b = createEmptySensor();

    expect(a.key).not.toBe(b.key);
    expect(a).toMatchObject({ label: "", sensorId: "", sensorToken: "", streamSinceId: 0 });
    expect(a.settings).toEqual(DEFAULT_SETTINGS);
  });

  it("falls back to a timestamp key when crypto.randomUUID is unavailable", () => {
    vi.stubGlobal("crypto", {});

    expect(createEmptySensor().key).toMatch(/^sensor_\d+_[0-9a-f]+$/);
  });
});
