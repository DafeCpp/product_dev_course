import { afterEach, describe, expect, it, vi } from "vitest";
import { openTelemetryStream } from "./api";

describe("openTelemetryStream", () => {
  afterEach(() => vi.unstubAllGlobals());

  it("surfaces an unsuccessful SSE handshake with its HTTP response", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue(
        new Response("token expired", {
          status: 401,
          statusText: "Unauthorized",
        }),
      ),
    );

    await expect(
      openTelemetryStream(
        "11111111-1111-1111-1111-111111111111",
        "expired-token",
        0,
        new AbortController().signal,
      ),
    ).rejects.toThrow("HTTP 401: token expired");
  });
});
