/** Типы для экспериментов и runs */

export interface Experiment {
  id: string
  project_id: string
  name: string
  description?: string
  experiment_type?: string
  owner_id: string
  status: 'draft' | 'running' | 'succeeded' | 'failed' | 'archived'
  tags: string[]
  metadata: Record<string, any>
  created_at: string
  updated_at: string
}

export interface Run {
  id: string
  experiment_id: string
  name: string
  params: Record<string, any>
  status: 'draft' | 'running' | 'succeeded' | 'failed' | 'archived'
  started_at?: string
  finished_at?: string
  duration_seconds?: number
  notes?: string
  metadata: Record<string, any>
  created_at: string
  updated_at: string
}

export interface ExperimentCreate {
  project_id: string
  name: string
  description?: string
  experiment_type?: string
  tags?: string[]
  metadata?: Record<string, any>
}

export interface ExperimentUpdate {
  name?: string
  description?: string
  experiment_type?: string
  tags?: string[]
  metadata?: Record<string, any>
  status?: string
}

export interface RunCreate {
  name: string
  params: Record<string, any>
  notes?: string
  metadata?: Record<string, any>
}

export interface RunUpdate {
  name?: string
  params?: Record<string, any>
  notes?: string
  metadata?: Record<string, any>
  status?: string
}

export interface ExperimentsListResponse {
  experiments: Experiment[]
  total: number
  page: number
  page_size: number
}

export interface RunsListResponse {
  runs: Run[]
  total: number
  page: number
  page_size: number
}

/** Типы для аутентификации */

export interface User {
  id: string
  username: string
  email: string
  is_active: boolean
  created_at: string
}

export interface LoginRequest {
  username: string
  password: string
}

export interface AuthResponse {
  expires_in?: number
  refresh_expires_in?: number
  token_type?: string
  [key: string]: unknown
}

/** Типы для датчиков */

export type SensorStatus = 'registering' | 'active' | 'inactive' | 'archived'

export interface Sensor {
  id: string
  project_id: string
  name: string
  type: string
  input_unit: string
  display_unit: string
  status: SensorStatus
  token_preview?: string | null
  last_heartbeat?: string | null
  active_profile_id?: string | null
  calibration_notes?: string | null
  created_at: string
  updated_at: string
}

export interface SensorCreate {
  project_id: string
  name: string
  type: string
  input_unit: string
  display_unit: string
  calibration_notes?: string
  conversion_profile?: ConversionProfileInput
}

export interface SensorUpdate {
  name?: string
  type?: string
  input_unit?: string
  display_unit?: string
  calibration_notes?: string
}

export interface ConversionProfileInput {
  version: string
  kind: string
  payload: Record<string, any>
  status?: 'draft' | 'scheduled' | 'active' | 'deprecated'
  valid_from?: string
  valid_to?: string
}

export interface SensorRegisterResponse {
  sensor: Sensor
  token: string
}

export interface SensorTokenResponse {
  sensor: Sensor
  token: string
}

export interface SensorsListResponse {
  sensors: Sensor[]
  total: number
  page: number
  page_size: number
}

/** Типы для Capture Sessions */

export type CaptureSessionStatus = 'draft' | 'running' | 'failed' | 'succeeded' | 'archived' | 'backfilling'

export interface CaptureSession {
  id: string
  run_id: string
  project_id: string
  ordinal_number: number
  started_at?: string | null
  stopped_at?: string | null
  status: CaptureSessionStatus
  initiated_by?: string | null
  notes?: string | null
  archived: boolean
  created_at: string
  updated_at: string
}

export interface CaptureSessionCreate {
  project_id: string
  run_id: string
  ordinal_number: number
  status?: CaptureSessionStatus
  notes?: string
}

export interface CaptureSessionsListResponse {
  capture_sessions: CaptureSession[]
  total: number
  page?: number
  page_size?: number
}

/** Типы для телеметрии */

export interface TelemetryReading {
  timestamp: string
  raw_value: number
  physical_value?: number
  meta: Record<string, any>
}

export interface TelemetryIngest {
  sensor_id: string
  run_id?: string
  capture_session_id?: string
  meta?: Record<string, any>
  readings: TelemetryReading[]
}

export interface TelemetryIngestResponse {
  status: 'accepted'
  accepted: number
}

