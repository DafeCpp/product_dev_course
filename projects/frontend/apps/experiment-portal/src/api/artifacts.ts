/** Artifacts and Run Sensors API */
import type {
  Artifact,
  ArtifactsListResponse,
  CreateArtifactRequest,
  RunSensor,
} from '../types'
import { apiGet, apiPost, apiDelete } from './client'

export const artifactsApi = {
  list: async (
    runId: string,
    params?: { type?: string; limit?: number; offset?: number }
  ): Promise<ArtifactsListResponse> => {
    return await apiGet(`/api/v1/runs/${runId}/artifacts`, { params })
  },

  create: async (runId: string, data: CreateArtifactRequest): Promise<Artifact> => {
    return await apiPost(`/api/v1/runs/${runId}/artifacts`, data)
  },

  delete: async (artifactId: string): Promise<void> => {
    await apiDelete(`/api/v1/artifacts/${artifactId}`)
  },

  approve: async (artifactId: string): Promise<Artifact> => {
    return await apiPost(`/api/v1/artifacts/${artifactId}/approve`, {})
  },

  requestUploadUrl: async (
    runId: string,
    data: {
      filename: string
      content_type: string
      type: string
      size_bytes?: number
      metadata?: Record<string, unknown>
    }
  ): Promise<{ upload_url: string; artifact_id: string; s3_key: string }> => {
    return await apiPost(`/api/v1/runs/${runId}/artifacts/upload-url`, data)
  },

  getDownloadUrl: async (
    artifactId: string
  ): Promise<{ download_url: string; expires_in: number | null }> => {
    return await apiGet(`/api/v1/artifacts/${artifactId}/download-url`)
  },
}

export const runSensorsApi = {
  list: async (runId: string, params?: { project_id?: string }): Promise<{ sensors: RunSensor[] }> => {
    return await apiGet(`/api/v1/runs/${runId}/sensors`, { params })
  },
  attach: async (runId: string, sensorId: string, params?: { project_id?: string }): Promise<RunSensor> => {
    return await apiPost(`/api/v1/runs/${runId}/sensors/${sensorId}`, {}, { params })
  },
  detach: async (runId: string, sensorId: string, params?: { project_id?: string }): Promise<void> => {
    await apiDelete(`/api/v1/runs/${runId}/sensors/${sensorId}`, { params })
  },
}
