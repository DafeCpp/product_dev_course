/** Sensors, Conversion Profiles, and Backfill API */
import type {
  Sensor,
  SensorCreate,
  SensorUpdate,
  SensorsListResponse,
  SensorRegisterResponse,
  SensorTokenResponse,
  StatusSummary,
  HeartbeatHistory,
  ConversionProfile,
  ConversionProfileInput,
  ConversionProfilesListResponse,
  BackfillTask,
  BackfillTasksListResponse,
  SensorErrorLogResponse,
  SensorProjectRequest,
  SensorProjectRequestListResponse,
  SensorProjectRequestStatus,
} from '../types'
import { apiGet, apiPost, apiPatch, apiDelete } from './client'

export const sensorsApi = {
  list: async (params?: {
    project_id?: string
    status?: string
    /** Backend pagination (preferred). */
    limit?: number
    offset?: number
    page?: number
    page_size?: number
  }): Promise<SensorsListResponse> => {
    return await apiGet('/api/v1/sensors', { params })
  },

  get: async (id: string, params?: { project_id?: string }): Promise<Sensor> => {
    return await apiGet(`/api/v1/sensors/${id}`, { params })
  },

  create: async (data: SensorCreate): Promise<SensorRegisterResponse> => {
    return await apiPost('/api/v1/sensors', data)
  },

  update: async (id: string, data: SensorUpdate, params?: { project_id?: string }): Promise<Sensor> => {
    return await apiPatch(`/api/v1/sensors/${id}`, data, { params })
  },

  delete: async (id: string, params?: { project_id?: string }): Promise<void> => {
    await apiDelete(`/api/v1/sensors/${id}`, { params })
  },

  rotateToken: async (id: string, params?: { project_id?: string }): Promise<SensorTokenResponse> => {
    return await apiPost(`/api/v1/sensors/${id}/rotate-token`, {}, { params })
  },

  getStatusSummary: async (projectId: string): Promise<StatusSummary> => {
    return await apiGet('/api/v1/sensors/status-summary', { params: { project_id: projectId } })
  },

  getHeartbeatHistory: async (sensorId: string, minutes: number = 60): Promise<HeartbeatHistory> => {
    return await apiGet(`/api/v1/sensors/${sensorId}/heartbeat-history`, { params: { minutes } })
  },

  // Multiple projects management
  getProjects: async (id: string): Promise<{ project_ids: string[] }> => {
    // IMPORTANT: use apiGet so project_id is auto-attached (auth-proxy derives X-Project-* from it)
    return await apiGet(`/api/v1/sensors/${id}/projects`)
  },

  /** Opens a request to attach the sensor to projectId; it is attached once a
   * project that already owns the sensor approves it. */
  addProject: async (id: string, projectId: string): Promise<SensorProjectRequest> => {
    return await apiPost(`/api/v1/sensors/${id}/projects`, { project_id: projectId })
  },

  listProjectRequests: async (params: {
    project_id: string
    direction: 'incoming' | 'outgoing'
    status?: SensorProjectRequestStatus
  }): Promise<SensorProjectRequestListResponse> => {
    return await apiGet('/api/v1/sensor-project-requests', { params })
  },

  /** Approve or reject from projectId, a project the sensor already belongs to. */
  decideProjectRequest: async (
    requestId: string,
    decision: 'approve' | 'reject',
    projectId: string
  ): Promise<SensorProjectRequest> => {
    return await apiPost(
      `/api/v1/sensor-project-requests/${requestId}/${decision}`,
      {},
      { params: { project_id: projectId } }
    )
  },

  /** Withdraw a request from the project that made it. */
  cancelProjectRequest: async (requestId: string, projectId: string): Promise<SensorProjectRequest> => {
    return await apiPost(
      `/api/v1/sensor-project-requests/${requestId}/cancel`,
      {},
      { params: { project_id: projectId } }
    )
  },

  removeProject: async (id: string, projectId: string): Promise<void> => {
    // Use explicit project_id context for permission checks in that project
    await apiDelete(`/api/v1/sensors/${id}/projects/${projectId}`, { params: { project_id: projectId } })
  },

  getErrorLog: async (
    sensorId: string,
    params?: { limit?: number; offset?: number }
  ): Promise<SensorErrorLogResponse> => {
    return await apiGet(`/api/v1/sensors/${sensorId}/error-log`, { params })
  },
}

export const conversionProfilesApi = {
  list: async (sensorId: string, params?: {
    limit?: number
    offset?: number
  }): Promise<ConversionProfilesListResponse> => {
    return await apiGet(`/api/v1/sensors/${sensorId}/conversion-profiles`, { params })
  },

  create: async (sensorId: string, data: ConversionProfileInput): Promise<ConversionProfile> => {
    return await apiPost(`/api/v1/sensors/${sensorId}/conversion-profiles`, data)
  },

  publish: async (sensorId: string, profileId: string): Promise<ConversionProfile> => {
    return await apiPost(`/api/v1/sensors/${sensorId}/conversion-profiles/${profileId}/publish`, {})
  },
}

export const backfillApi = {
  start: async (sensorId: string): Promise<BackfillTask> => {
    return await apiPost(`/api/v1/sensors/${sensorId}/backfill`, {})
  },

  list: async (sensorId: string, params?: {
    limit?: number
    offset?: number
  }): Promise<BackfillTasksListResponse> => {
    return await apiGet(`/api/v1/sensors/${sensorId}/backfill`, { params })
  },

  get: async (sensorId: string, taskId: string): Promise<BackfillTask> => {
    return await apiGet(`/api/v1/sensors/${sensorId}/backfill/${taskId}`)
  },
}
