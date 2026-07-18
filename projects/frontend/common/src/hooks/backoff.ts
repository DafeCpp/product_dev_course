export interface TelemetryBackoffOptions {
  initialDelayMs?: number
  maxDelayMs?: number
  factor?: number
  jitterRatio?: number
  maxAttempts?: number
}

export function computeBackoffDelayMs(attempt: number, opts?: TelemetryBackoffOptions): number {
  const { initialDelayMs = 1000, factor = 2, maxDelayMs = 30000, jitterRatio = 0.2 } = opts ?? {}
  const base = Math.min(maxDelayMs, initialDelayMs * factor ** Math.max(0, attempt - 1))
  const jitter = base * jitterRatio * (Math.random() * 2 - 1)
  return Math.max(0, Math.round(base + jitter))
}
