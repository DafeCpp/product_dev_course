/** Типы для экспериментов и runs */
import type { TelemetryQueryRecord } from './telemetry'

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
  tags?: string[]
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
  password_change_required?: boolean
  created_at: string
}

export interface LoginRequest {
  username: string
  password: string
}

export interface RegisterRequest {
  username: string
  email: string
  password: string
}

export interface AuthResponse {
  user?: User
  expires_in?: number
  refresh_expires_in?: number
  token_type?: string
  access_token?: string
  refresh_token?: string
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

export type ConversionProfileStatus = 'draft' | 'scheduled' | 'active' | 'deprecated'

export interface ConversionProfile {
  id: string
  sensor_id: string
  project_id: string
  version: string
  kind: string
  payload: Record<string, any>
  status: ConversionProfileStatus
  valid_from?: string | null
  valid_to?: string | null
  created_by: string
  published_by?: string | null
  created_at: string
  updated_at: string
}

export interface ConversionProfilesListResponse {
  conversion_profiles: ConversionProfile[]
  total: number
  limit: number
  offset: number
}

export type BackfillTaskStatus = 'pending' | 'running' | 'completed' | 'failed'

export interface BackfillTask {
  id: string
  sensor_id: string
  project_id: string
  conversion_profile_id: string
  status: BackfillTaskStatus
  total_records: number | null
  processed_records: number
  error_message: string | null
  created_by: string
  created_at: string
  started_at: string | null
  completed_at: string | null
}

export interface BackfillTasksListResponse {
  backfill_tasks: BackfillTask[]
  total: number
  limit: number
  offset: number
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

export type { TelemetryStreamRecord, TelemetryQueryRecord } from './telemetry'

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

/** Типы для проектов */

export interface Project {
  id: string
  name: string
  description?: string | null
  owner_id: string
  created_at: string
  updated_at: string
}

export interface ProjectCreate {
  name: string
  description?: string
}

export interface ProjectUpdate {
  name?: string
  description?: string
}

export interface ProjectsListResponse {
  projects: Project[]
}

export interface ProjectMember {
  project_id: string
  user_id: string
  role: 'owner' | 'editor' | 'viewer'
  created_at: string
  username?: string | null
}

export interface ProjectMemberAdd {
  user_id: string
  role: 'owner' | 'editor' | 'viewer'
}

export interface ProjectMemberUpdate {
  role: 'owner' | 'editor' | 'viewer'
}

export interface ProjectMembersListResponse {
  members: ProjectMember[]
}

/** Типы для аудит-лога (events) */

export interface RunEvent {
  id: number
  run_id: string
  event_type: string
  actor_id: string
  actor_role: string
  payload: Record<string, any>
  created_at: string
}

export interface RunEventsListResponse {
  events: RunEvent[]
  total: number
  page: number
  page_size: number
}

export interface CaptureSessionEvent {
  id: number
  capture_session_id: string
  event_type: string
  actor_id: string
  actor_role: string
  payload: Record<string, any>
  created_at: string
}

export interface CaptureSessionEventsListResponse {
  events: CaptureSessionEvent[]
  total: number
  page: number
  page_size: number
}

/** Типы для вебхуков */

export interface WebhookSubscription {
  id: string
  project_id: string
  target_url: string
  secret: string | null
  event_types: string[]
  is_active: boolean
  created_at: string
  updated_at: string
}

export interface WebhookSubscriptionCreate {
  target_url: string
  event_types: string[]
  secret?: string
}

export interface WebhooksListResponse {
  webhooks: WebhookSubscription[]
  total: number
  page: number
  page_size: number
}

export interface WebhookDelivery {
  id: string
  subscription_id: string
  project_id: string
  event_type: string
  target_url: string
  secret: string | null
  request_body: Record<string, any>
  status: string
  attempt_count: number
  last_error: string | null
  dedup_key: string | null
  locked_at: string | null
  next_attempt_at: string
  created_at: string
  updated_at: string
}

export interface WebhookDeliveriesListResponse {
  deliveries: WebhookDelivery[]
  total: number
  page: number
  page_size: number
}

