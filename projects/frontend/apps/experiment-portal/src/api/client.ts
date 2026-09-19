/** API клиент для взаимодействия с бэкендом через Auth Proxy */
import type { AxiosRequestConfig } from 'axios'
import { getActiveProjectId } from '../utils/activeProject'
import { createAuthProxyClient } from './http/axiosInstance'
import { attachSessionInterceptors } from './http/session'

export const apiClient = createAuthProxyClient({ timeout: 30000, skipDebugToast: true })

function _extractProjectIdFromData(data: unknown): string | undefined {
  if (!data) return undefined
  if (typeof data === 'object' && data !== null && !Array.isArray(data)) {
    const projectId = (data as any).project_id
    return typeof projectId === 'string' && projectId ? projectId : undefined
  }
  if (typeof data === 'string') {
    // иногда axios/transform могут давать строку, пробуем распарсить JSON
    try {
      const parsed = JSON.parse(data)
      const projectId = parsed?.project_id
      return typeof projectId === 'string' && projectId ? projectId : undefined
    } catch {
      return undefined
    }
  }
  return undefined
}

function _withProjectIdConfig(
  url: string,
  config: AxiosRequestConfig | undefined,
  data?: unknown
): AxiosRequestConfig | undefined {
  // Проставляем project_id только для experiment-service API (через auth-proxy)
  if (!url.startsWith('/api/')) return config

  const existing = (config?.params as any)?.project_id
  if (existing) return config

  const projectId = _extractProjectIdFromData(data) || getActiveProjectId() || undefined
  if (!projectId) return config

  const next: AxiosRequestConfig = { ...(config || {}) }
  next.params = { ...(next.params as any), project_id: projectId }
  return next
}

export async function apiGet<T = any>(url: string, config?: AxiosRequestConfig): Promise<T> {
  const cfg = _withProjectIdConfig(url, config)
  const response = cfg ? await apiClient.get(url, cfg) : await apiClient.get(url)
  return response.data
}

export async function apiPost<T = any>(
  url: string,
  data?: any,
  config?: AxiosRequestConfig
): Promise<T> {
  const cfg = _withProjectIdConfig(url, config, data)
  const response = cfg ? await apiClient.post(url, data, cfg) : await apiClient.post(url, data)
  return response.data
}

export async function apiPatch<T = any>(
  url: string,
  data?: any,
  config?: AxiosRequestConfig
): Promise<T> {
  const cfg = _withProjectIdConfig(url, config, data)
  const response = cfg ? await apiClient.patch(url, data, cfg) : await apiClient.patch(url, data)
  return response.data
}

export async function apiDelete<T = any>(url: string, config?: AxiosRequestConfig): Promise<T> {
  const cfg = _withProjectIdConfig(url, config)
  const response = cfg ? await apiClient.delete(url, cfg) : await apiClient.delete(url)
  return response.data
}

attachSessionInterceptors(apiClient)

// Domain API re-exports — implementations live in focused domain modules.
// Callers that import from './client' continue to work unchanged.
export { experimentsApi } from './experiments'
export { runsApi } from './runs'
export { sensorsApi, conversionProfilesApi, backfillApi } from './sensors'
export { telemetryApi, telemetryExportApi, captureSessionsApi, runEventsApi, captureSessionEventsApi } from './telemetry'
export { projectsApi, usersApi } from './projects'
export { metricsApi } from './metrics'
export { webhooksApi } from './webhooks'
export { comparisonApi } from './comparison'
export { artifactsApi, runSensorsApi } from './artifacts'

