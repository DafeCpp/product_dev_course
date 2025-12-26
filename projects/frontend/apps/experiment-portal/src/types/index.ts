/** Типы для экспериментов и runs */

export interface Experiment {
  id: string
  project_id: string
  name: string
  description?: string
  experiment_type?: string
  created_by: string
  status: 'created' | 'running' | 'completed' | 'failed' | 'archived'
  tags: string[]
  metadata: Record<string, any>
  created_at: string
  updated_at: string
}

export interface Run {
  id: string
  experiment_id: string
  name: string
  parameters: Record<string, any>
  status: 'created' | 'running' | 'completed' | 'failed'
  started_at?: string
  completed_at?: string
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
  parameters: Record<string, any>
  notes?: string
  metadata?: Record<string, any>
}

export interface RunUpdate {
  name?: string
  parameters?: Record<string, any>
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

