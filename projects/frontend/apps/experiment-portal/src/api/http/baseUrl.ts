export const AUTH_PROXY_URL =
  import.meta.env.VITE_AUTH_PROXY_URL ?? 'http://localhost:8080'

export const TELEMETRY_BASE_URL =
  import.meta.env.VITE_TELEMETRY_INGEST_URL || AUTH_PROXY_URL
