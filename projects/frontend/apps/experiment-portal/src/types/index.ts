/**
 * Типы API. Модели experiment-service и auth-service берутся из сгенерированных
 * `generated/*.ts` (`npm run generate:types`, CI проверяет актуальность), поэтому
 * здесь только алиасы под привычные имена и типы, которых нет в спеках.
 */
import type { components as ExperimentApi } from './generated/experiment-service'
import type { components as AuthApi } from './generated/auth-service'
import type { UserProjectRole } from './permissions'
export type { UserProjectRole }

type Es = ExperimentApi['schemas']
type As = AuthApi['schemas']

/** Типы для экспериментов и runs */

export type Experiment = Es['Experiment']
export type ExperimentCreate = Es['ExperimentCreate']
export type ExperimentUpdate = Es['ExperimentUpdate']
export type ExperimentsListResponse = Es['ExperimentListResponse']

export type Run = Es['Run']
export type RunCreate = Es['RunCreate']
export type RunUpdate = Es['RunUpdate']
export type RunsListResponse = Es['RunListResponse']
export type RunSensor = Es['RunSensor']

/** Типы для аутентификации */

export type User = As['User']
/** Тот же формат, что и у `User`: GET /auth/admin/users отдаёт UserResponse. */
export type AdminUser = As['User']
export type LoginRequest = As['UserLoginRequest']
export type RegisterRequest = As['UserRegisterRequest']

/** Ответ auth-proxy: токены вырезаются BFF и до портала не доходят. */
export type AuthResponse = Partial<Omit<As['AuthResponse'], 'access_token' | 'refresh_token'>>

/** Эндпоинты /auth/admin/invites и /api/v1/users/search не описаны в спеке auth-service. */
export interface AdminInviteToken {
  id: string
  token: string
  created_by: string
  email_hint: string | null
  expires_at: string
  used_at: string | null
  used_by: string | null
  created_at: string
  is_active: boolean
}

export interface UserSearchResult {
  id: string
  username: string
  email: string
}

/** Типы для датчиков */

export type SensorStatus = Es['SensorStatus']
export type ConnectionStatus = Es['ConnectionStatus']
export type Sensor = Es['Sensor']
export type StatusSummary = Es['SensorStatusSummary']
export type HeartbeatHistory = Es['SensorHeartbeatHistory']
export type SensorCreate = Es['SensorRegisterRequest']
export type SensorUpdate = Es['SensorUpdate']
export type SensorRegisterResponse = Es['SensorTokenResponse']
export type SensorTokenResponse = Es['SensorTokenResponse']
export type SensorsListResponse = Es['SensorListResponse']

/** Журнал ошибок датчика отдаёт не experiment-service, в его спеке эндпоинта нет. */
export interface SensorErrorEntry {
  id: number
  sensor_id: string
  occurred_at: string
  error_code: string
  error_message: string | null
  endpoint: string
  readings_count: number | null
  meta: Record<string, unknown>
}

export interface SensorErrorLogResponse {
  sensor_id: string
  entries: SensorErrorEntry[]
  total: number
  limit: number
  offset: number
}

export type ConversionProfileInput = Es['ConversionProfileInput']
export type ConversionProfileStatus = Es['ConversionProfileStatus']
export type ConversionProfile = Es['ConversionProfile']
export type ConversionProfilesListResponse = Es['ConversionProfileListResponse']

export type BackfillTaskStatus = Es['BackfillTaskStatus']
export type BackfillTask = Es['BackfillTask']
export type BackfillTasksListResponse = Es['BackfillTaskListResponse']

/** Типы для Capture Sessions */

export type CaptureSessionStatus = Es['CaptureSessionStatus']
export type CaptureSession = Es['CaptureSession']
export type CaptureSessionCreate = Es['CaptureSessionCreate']
export type CaptureSessionsListResponse = Es['CaptureSessionListResponse']

/** Типы для телеметрии */

export type TelemetryReading = Es['TelemetryIngestRequest']['readings'][number]
export type TelemetryIngest = Es['TelemetryIngestRequest']

/** Ответ отдаёт telemetry-ingest-service, в спеке experiment-service его нет. */
export interface TelemetryIngestResponse {
  status: 'accepted'
  accepted: number
}

export type { TelemetryStreamRecord, TelemetryQueryRecord } from './telemetry'
export type {
  TelemetryQueryResponse,
  TelemetryAggregatedRecord,
  TelemetryAggregatedResponse,
} from 'frontend-common'

/** Типы для проектов */

export type Project = As['Project']
export type ProjectCreate = As['ProjectCreateRequest']
export type ProjectUpdate = As['ProjectUpdateRequest']

/** Адаптер: auth-service отдаёт `items`, `projectsApi` приводит их к `projects`. */
export interface ProjectsListResponse {
  projects: Project[]
  total?: number
}

export type ProjectMember = As['ProjectMember']
export type ProjectMemberAdd = As['ProjectMemberAddRequest']
export type ProjectMemberUpdate = As['ProjectMemberUpdateRequest']
export type ProjectMembersListResponse = As['ProjectMemberListResponse']
export type ProjectRoleAssignment = As['ProjectRoleAssignment']

/** Типы для аудит-лога (events) */

export type RunEvent = Es['RunEvent']
export type RunEventsListResponse = Es['RunEventListResponse']
export type CaptureSessionEvent = Es['CaptureSessionEvent']
export type CaptureSessionEventsListResponse = Es['CaptureSessionEventListResponse']

/** Типы для метрик */

export type RunMetricsResponse = Es['MetricQueryResponse']
export type MetricSummaryItem = Es['MetricSummaryItem']
export type MetricSummaryResponse = Es['MetricSummaryResponse']
export type MetricBucket = Es['MetricBucket']
export type MetricAggregationsResponse = Es['MetricAggregationsResponse']

/** Типы для сравнения runs */

export type ComparisonMetricSummary = Es['ComparisonMetricSummary']
export type ComparisonMetricPoint = Es['ComparisonPoint']
export type ComparisonMetricData = Es['ComparisonMetricData']
export type ComparisonRunEntry = Es['ComparisonRunEntry']
export type ComparisonResponse = Es['ComparisonResponse']

/** Типы для артефактов */

/** Подсказка для UI: в API `type` — произвольная строка. */
export type ArtifactType = 'model' | 'dataset' | 'plot' | 'log' | 'config' | 'other'

export type Artifact = Es['Artifact']
export type ArtifactsListResponse = Es['ArtifactListResponse']
export type CreateArtifactRequest = Es['ArtifactCreate']

/** Типы для вебхуков */

export type WebhookSubscription = Es['WebhookSubscription']
export type WebhookSubscriptionCreate = Es['WebhookCreate']
export type WebhooksListResponse = Es['WebhookListResponse']
export type WebhookDelivery = Es['WebhookDelivery']
export type WebhookDeliveriesListResponse = Es['WebhookDeliveryListResponse']
