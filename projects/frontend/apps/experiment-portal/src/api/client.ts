/** API клиент для взаимодействия с бэкендом через Auth Proxy */
import axios from 'axios'
import type { AxiosRequestConfig } from 'axios'
import type {
  Experiment,
  ExperimentCreate,
  ExperimentUpdate,
  ExperimentsListResponse,
  Run,
  RunCreate,
  RunUpdate,
  RunsListResponse,
  Sensor,
  SensorCreate,
  SensorUpdate,
  SensorsListResponse,
  SensorRegisterResponse,
  SensorTokenResponse,
  CaptureSession,
  CaptureSessionCreate,
  CaptureSessionsListResponse,
  TelemetryIngest,
  TelemetryIngestResponse,
  Project,
  ProjectCreate,
  ProjectUpdate,
  ProjectsListResponse,
  ProjectMember,
  ProjectMemberAdd,
  ProjectMemberUpdate,
  ProjectMembersListResponse,
} from '../types'
import { generateRequestId } from '../utils/uuid'
import { getTraceId } from '../utils/trace'
import { getActiveProjectId } from '../utils/activeProject'

// API работает через Auth Proxy, который автоматически добавляет токен из куки
const AUTH_PROXY_URL = import.meta.env.VITE_AUTH_PROXY_URL || 'http://localhost:8080'

const apiClient = axios.create({
  baseURL: AUTH_PROXY_URL,
  headers: {
    'Content-Type': 'application/json',
  },
  withCredentials: true, // Важно для работы с HttpOnly куками
  timeout: 30000, // 30 секунд таймаут
})

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

async function apiGet<T = any>(url: string, config?: AxiosRequestConfig): Promise<T> {
  const cfg = _withProjectIdConfig(url, config)
  const response = cfg ? await apiClient.get(url, cfg) : await apiClient.get(url)
  return response.data
}

async function apiPost<T = any>(
  url: string,
  data?: any,
  config?: AxiosRequestConfig
): Promise<T> {
  const cfg = _withProjectIdConfig(url, config, data)
  const response = cfg ? await apiClient.post(url, data, cfg) : await apiClient.post(url, data)
  return response.data
}

async function apiPatch<T = any>(
  url: string,
  data?: any,
  config?: AxiosRequestConfig
): Promise<T> {
  const cfg = _withProjectIdConfig(url, config, data)
  const response = cfg ? await apiClient.patch(url, data, cfg) : await apiClient.patch(url, data)
  return response.data
}

async function apiDelete<T = any>(url: string, config?: AxiosRequestConfig): Promise<T> {
  const cfg = _withProjectIdConfig(url, config)
  const response = cfg ? await apiClient.delete(url, cfg) : await apiClient.delete(url)
  return response.data
}

// Interceptor для добавления trace_id и request_id в заголовки
apiClient.interceptors.request.use(
  (config) => {
    // Генерируем request_id для каждого запроса
    const requestId = generateRequestId()
    const traceId = getTraceId()

    // Добавляем заголовки
    config.headers['X-Trace-Id'] = traceId
    config.headers['X-Request-Id'] = requestId

    // Логирование запроса (только в development)
    if (import.meta.env.DEV) {
      console.log({
        trace_id: traceId,
        request_id: requestId,
        method: config.method?.toUpperCase(),
        url: config.url,
        baseURL: config.baseURL,
      })
    }

    return config
  },
  (error) => {
    return Promise.reject(error)
  }
)

// Обработка ошибок и автоматическое обновление токена
apiClient.interceptors.response.use(
  (response) => {
    // Логирование успешных ответов
    console.log('API response SUCCESS:', {
      url: response.config.url,
      method: response.config.method?.toUpperCase(),
      status: response.status,
      statusText: response.statusText,
      headers: response.headers,
      data: response.data,
    })
    return response
  },
  async (error) => {
    // Логирование ошибок
    console.error('API response ERROR:', {
      url: error.config?.url,
      method: error.config?.method?.toUpperCase(),
      status: error.response?.status,
      statusText: error.response?.statusText,
      data: error.response?.data,
      message: error.message,
      code: error.code,
    })
    const originalRequest = error.config
    const traceId = error.config?.headers?.['X-Trace-Id'] as string | undefined
    const requestId = error.config?.headers?.['X-Request-Id'] as string | undefined

    // Логирование ошибки
    console.error({
      trace_id: traceId,
      request_id: requestId,
      error: error.message,
      status: error.response?.status,
      statusText: error.response?.statusText,
      url: error.config?.url,
      method: error.config?.method?.toUpperCase(),
    })

    // Если получили 401 и это не повторный запрос
    if (error.response?.status === 401 && !originalRequest._retry) {
      originalRequest._retry = true

      try {
        // Пытаемся обновить токен через Auth Proxy
        const authProxyUrl = import.meta.env.VITE_AUTH_PROXY_URL || 'http://localhost:8080'
        await axios.post(
          `${authProxyUrl}/auth/refresh`,
          {},
          { withCredentials: true }
        )

        // Повторяем оригинальный запрос
        return apiClient(originalRequest)
      } catch (refreshError) {
        // Если refresh не удался - перенаправляем на страницу входа
        window.location.href = '/login'
        return Promise.reject(refreshError)
      }
    }

    return Promise.reject(error)
  }
)

// Experiments API
export const experimentsApi = {
  list: async (params?: {
    project_id?: string
    status?: string
    tags?: string
    page?: number
    page_size?: number
  }): Promise<ExperimentsListResponse> => {
    return await apiGet('/api/v1/experiments', { params })
  },

  get: async (id: string): Promise<Experiment> => {
    return await apiGet(`/api/v1/experiments/${id}`)
  },

  create: async (data: ExperimentCreate): Promise<Experiment> => {
    return await apiPost('/api/v1/experiments', data)
  },

  update: async (id: string, data: ExperimentUpdate): Promise<Experiment> => {
    return await apiPatch(`/api/v1/experiments/${id}`, data)
  },

  delete: async (id: string): Promise<void> => {
    await apiDelete(`/api/v1/experiments/${id}`)
  },

  search: async (params: {
    q?: string
    project_id?: string
    page?: number
    page_size?: number
  }): Promise<ExperimentsListResponse> => {
    return await apiGet('/api/v1/experiments/search', { params })
  },
}

// Runs API
export const runsApi = {
  list: async (
    experimentId: string,
    params?: {
      status?: string
      page?: number
      page_size?: number
    }
  ): Promise<RunsListResponse> => {
    return await apiGet(`/api/v1/experiments/${experimentId}/runs`, {
      params,
    })
  },

  get: async (id: string): Promise<Run> => {
    return await apiGet(`/api/v1/runs/${id}`)
  },

  create: async (experimentId: string, data: RunCreate): Promise<Run> => {
    return await apiPost(`/api/v1/experiments/${experimentId}/runs`, data)
  },

  update: async (id: string, data: RunUpdate): Promise<Run> => {
    return await apiPatch(`/api/v1/runs/${id}`, data)
  },

  complete: async (id: string): Promise<Run> => {
    return await apiPatch(`/api/v1/runs/${id}`, { status: 'succeeded' })
  },

  fail: async (id: string, reason?: string): Promise<Run> => {
    return await apiPatch(`/api/v1/runs/${id}`, { status: 'failed', reason })
  },
}

// Sensors API
export const sensorsApi = {
  list: async (params?: {
    project_id?: string
    status?: string
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

  // Multiple projects management
  getProjects: async (id: string): Promise<{ project_ids: string[] }> => {
    const response = await apiClient.get(`/api/v1/sensors/${id}/projects`)
    return response.data
  },

  addProject: async (id: string, projectId: string): Promise<void> => {
    await apiPost(`/api/v1/sensors/${id}/projects`, { project_id: projectId })
  },

  removeProject: async (id: string, projectId: string): Promise<void> => {
    await apiDelete(`/api/v1/sensors/${id}/projects/${projectId}`)
  },
}

// Capture Sessions API
export const captureSessionsApi = {
  list: async (runId: string, params?: {
    page?: number
    page_size?: number
  }): Promise<CaptureSessionsListResponse> => {
    return await apiGet(`/api/v1/runs/${runId}/capture-sessions`, { params })
  },

  get: async (runId: string, sessionId: string): Promise<CaptureSession> => {
    return await apiGet(`/api/v1/runs/${runId}/capture-sessions/${sessionId}`)
  },

  create: async (runId: string, data: CaptureSessionCreate, params?: { project_id?: string }): Promise<CaptureSession> => {
    return await apiPost(`/api/v1/runs/${runId}/capture-sessions`, data, { params })
  },

  stop: async (runId: string, sessionId: string): Promise<CaptureSession> => {
    return await apiPost(`/api/v1/runs/${runId}/capture-sessions/${sessionId}/stop`)
  },

  delete: async (runId: string, sessionId: string): Promise<void> => {
    await apiDelete(`/api/v1/runs/${runId}/capture-sessions/${sessionId}`)
  },
}

// Telemetry API
export const telemetryApi = {
  ingest: async (data: TelemetryIngest, sensorToken: string): Promise<TelemetryIngestResponse> => {
    // Для телеметрии используется прямой запрос к Telemetry Ingest Service с токеном датчика
    // (не через Auth Proxy, так как это публичный endpoint)
    const TELEMETRY_INGEST_URL = import.meta.env.VITE_TELEMETRY_INGEST_URL || 'http://localhost:8003'
    const requestId = generateRequestId()
    const traceId = getTraceId()

    const response = await axios.post(
      `${TELEMETRY_INGEST_URL}/api/v1/telemetry`,
      data,
      {
        headers: {
          'Authorization': `Bearer ${sensorToken}`,
          'Content-Type': 'application/json',
          'X-Trace-Id': traceId,
          'X-Request-Id': requestId,
        },
      }
    )
    return response.data
  },
}

// Projects API
export const projectsApi = {
  list: async (): Promise<ProjectsListResponse> => {
    const response = await apiClient.get<ProjectsListResponse>('/projects')
    return response.data
  },

  get: async (id: string): Promise<Project> => {
    const response = await apiClient.get<Project>(`/projects/${id}`)
    return response.data
  },

  create: async (data: ProjectCreate): Promise<Project> => {
    const response = await apiClient.post<Project>('/projects', data)
    return response.data
  },

  update: async (id: string, data: ProjectUpdate): Promise<Project> => {
    const response = await apiClient.put<Project>(`/projects/${id}`, data)
    return response.data
  },

  delete: async (id: string): Promise<void> => {
    await apiClient.delete(`/projects/${id}`)
  },

  listMembers: async (projectId: string): Promise<ProjectMembersListResponse> => {
    const response = await apiClient.get<ProjectMembersListResponse>(
      `/projects/${projectId}/members`
    )
    return response.data
  },

  addMember: async (projectId: string, data: ProjectMemberAdd): Promise<ProjectMember> => {
    const response = await apiClient.post<ProjectMember>(
      `/projects/${projectId}/members`,
      data
    )
    return response.data
  },

  removeMember: async (projectId: string, userId: string): Promise<void> => {
    await apiClient.delete(`/projects/${projectId}/members/${userId}`)
  },

  updateMemberRole: async (
    projectId: string,
    userId: string,
    data: ProjectMemberUpdate
  ): Promise<ProjectMember> => {
    const response = await apiClient.put<ProjectMember>(
      `/projects/${projectId}/members/${userId}/role`,
      data
    )
    return response.data
  },
}

