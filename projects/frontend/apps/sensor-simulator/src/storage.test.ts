import { beforeEach, describe, expect, it } from "vitest";
import { loadPersistedState } from "./storage";
import { STORAGE_KEY } from "./domain";

describe("sensor storage", () => {
  beforeEach(() => localStorage.clear());
  it("migrates v1 settings to v2 sensors", () => {
    localStorage.setItem(
      STORAGE_KEY,
      JSON.stringify({
        version: 1,
        settings: { seed: 9 },
        sensors: [{ key: "old" }],
        selectedSensorKey: "old",
      }),
    );
    const state = loadPersistedState();
    expect(state?.version).toBe(2);
    expect(state?.sensors[0].settings.seed).toBe(9);
  });
  it("falls back on malformed data", () => {
    localStorage.setItem(STORAGE_KEY, "{bad");
    expect(loadPersistedState()).toBeNull();
  });
  it("drops invalid sensors and creates a default sensor", () => {
    localStorage.setItem(
      STORAGE_KEY,
      JSON.stringify({ version: 2, sensors: [{ nope: true }] }),
    );
    expect(loadPersistedState()?.sensors).toHaveLength(1);
  });
});
