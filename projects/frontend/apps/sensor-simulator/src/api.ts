import { TELEMETRY_BASE, type TelemetryIngestBody } from "./domain";
export async function postTelemetry(body: TelemetryIngestBody, token: string) {
  const response = await fetch(`${TELEMETRY_BASE}/api/v1/telemetry`, {
    method: "POST",
    headers: {
      Authorization: `Bearer ${token}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify(body),
  });
  return {
    ok: response.ok,
    status: response.status,
    text: await response.text(),
  };
}
export async function openTelemetryStream(
  sensorId: string,
  token: string,
  sinceId: number,
  signal: AbortSignal,
) {
  const url = new URL(
    `${TELEMETRY_BASE}/api/v1/telemetry/stream`,
    window.location.origin,
  );
  url.searchParams.set("sensor_id", sensorId);
  if (sinceId > 0) url.searchParams.set("since_id", String(sinceId));
  return fetch(url, { headers: { Authorization: `Bearer ${token}` }, signal });
}
