export interface TelemetryStreamRecord {
  id: number
  sensor_id: string
  project_id: string
  timestamp: string
  raw_value: number
  physical_value: number | null
  run_id: string | null
  capture_session_id: string | null
  meta: Record<string, unknown>
}

export type TelemetryQueryRecord = TelemetryStreamRecord

export interface TelemetryQueryResponse {
  points: TelemetryQueryRecord[]
  next_since_id: number | null
}

export interface TelemetryAggregatedRecord {
  bucket: string
  sensor_id: string | null
  signal: string | null
  capture_session_id: string | null
  sample_count: number
  avg_raw: number | null
  min_raw: number | null
  max_raw: number | null
  avg_physical: number | null
  min_physical: number | null
  max_physical: number | null
}

export interface TelemetryAggregatedResponse {
  buckets: TelemetryAggregatedRecord[]
  bucket_interval: string
}

/** Opens an SSE-based telemetry stream; caller supplies transport/auth wiring. */
export interface TelemetryStreamOpenParams {
  sensorId: string
  sinceTs?: string
  sinceId?: number
  idleTimeoutSeconds?: number
  signal: AbortSignal
}

export type TelemetryStreamOpener = (params: TelemetryStreamOpenParams) => Promise<{ response: Response }>

export type TelemetryQueryFn = (params: {
  capture_session_id: string
  sensor_id?: string[]
  since_id?: number
  limit?: number
  include_late?: boolean
  order?: 'asc' | 'desc'
}) => Promise<TelemetryQueryResponse>

export type TelemetryAggregatedFn = (params: {
  capture_session_id: string
  sensor_id?: string[]
  signal?: string
  time_from?: string
  time_to?: string
  limit?: number
  order?: 'asc' | 'desc'
}) => Promise<TelemetryAggregatedResponse>
