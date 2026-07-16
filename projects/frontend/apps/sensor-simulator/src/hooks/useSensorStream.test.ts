import { describe, expect, it } from "vitest";
import { streamIsActive } from "./useSensorStream";

describe("streamIsActive", () => {
  it.each(["connecting", "streaming", "reconnecting"] as const)(
    "keeps the UI locked while the stream is %s",
    (status) => {
      expect(streamIsActive("sensor_test", status)).toBe(true);
    },
  );

  it.each(["idle", "error", "stopped"] as const)(
    "releases the UI lock when the stream is %s",
    (status) => {
      expect(streamIsActive("sensor_test", status)).toBe(false);
    },
  );

  it("does not lock the UI without a selected active stream", () => {
    expect(streamIsActive(null, "streaming")).toBe(false);
  });
});
