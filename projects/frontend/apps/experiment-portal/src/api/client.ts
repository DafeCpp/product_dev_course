/** API клиент для взаимодействия с бэкендом через Auth Proxy */
import axios from 'axios'
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
    const response = await apiClient.get('/api/v1/experiments', { params })
    return response.data
  },

  get: async (id: string): Promise<Experiment> => {
    const response = await apiClient.get(`/api/v1/experiments/${id}`)
    return response.data
  },

  create: async (data: ExperimentCreate): Promise<Experiment> => {
    const response = await apiClient.post('/api/v1/experiments', data)
    return response.data
  },

  update: async (id: string, data: ExperimentUpdate): Promise<Experiment> => {
    const response = await apiClient.patch(`/api/v1/experiments/${id}`, data)
    return response.data
  },

  delete: async (id: string): Promise<void> => {
    await apiClient.delete(`/api/v1/experiments/${id}`)
  },

  search: async (params: {
    q?: string
    project_id?: string
    page?: number
    page_size?: number
  }): Promise<ExperimentsListResponse> => {
    const response = await apiClient.get('/api/v1/experiments/search', { params })
    return response.data
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
    const response = await apiClient.get(`/api/v1/experiments/${experimentId}/runs`, {
      params,
    })
    return response.data
  },

  get: async (id: string): Promise<Run> => {
    const response = await apiClient.get(`/api/v1/runs/${id}`)
    return response.data
  },

  create: async (experimentId: string, data: RunCreate): Promise<Run> => {
    const response = await apiClient.post(
      `/api/v1/experiments/${experimentId}/runs`,
      data
    )
    return response.data
  },

  update: async (id: string, data: RunUpdate): Promise<Run> => {
    const response = await apiClient.patch(`/api/v1/runs/${id}`, data)
    return response.data
  },

  complete: async (id: string): Promise<Run> => {
    const response = await apiClient.patch(`/api/v1/runs/${id}`, { status: 'succeeded' })
    return response.data
  },

  fail: async (id: string, reason?: string): Promise<Run> => {
    const response = await apiClient.patch(`/api/v1/runs/${id}`, { status: 'failed', reason })
    return response.data
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
    const response = await apiClient.get('/api/v1/sensors', { params })
    return response.data
  },

  get: async (id: string, params?: { project_id?: string }): Promise<Sensor> => {
    const response = await apiClient.get(`/api/v1/sensors/${id}`, { params })
    return response.data
  },

  create: async (data: SensorCreate): Promise<SensorRegisterResponse> => {
    const response = await apiClient.post('/api/v1/sensors', data)
    return response.data
  },

  update: async (id: string, data: SensorUpdate, params?: { project_id?: string }): Promise<Sensor> => {
    const response = await apiClient.patch(`/api/v1/sensors/${id}`, data, { params })
    return response.data
  },

  delete: async (id: string, params?: { project_id?: string }): Promise<void> => {
    await apiClient.delete(`/api/v1/sensors/${id}`, { params })
  },

  rotateToken: async (id: string, params?: { project_id?: string }): Promise<SensorTokenResponse> => {
    const response = await apiClient.post(`/api/v1/sensors/${id}/rotate-token`, {}, { params })
    return response.data
  },

  // Multiple projects management
  getProjects: async (id: string): Promise<{ project_ids: string[] }> => {
    const response = await apiClient.get(`/api/v1/sensors/${id}/projects`)
    return response.data
  },

  addProject: async (id: string, projectId: string): Promise<void> => {
    await apiClient.post(`/api/v1/sensors/${id}/projects`, { project_id: projectId })
  },

  removeProject: async (id: string, projectId: string): Promise<void> => {
    await apiClient.delete(`/api/v1/sensors/${id}/projects/${projectId}`)
  },
}

// Capture Sessions API
export const captureSessionsApi = {
  list: async (runId: string, params?: {
    page?: number
    page_size?: number
  }): Promise<CaptureSessionsListResponse> => {
    const response = await apiClient.get(`/api/v1/runs/${runId}/capture-sessions`, { params })
    return response.data
  },

  get: async (runId: string, sessionId: string): Promise<CaptureSession> => {
    const response = await apiClient.get(`/api/v1/runs/${runId}/capture-sessions/${sessionId}`)
    return response.data
  },

  create: async (runId: string, data: CaptureSessionCreate, params?: { project_id?: string }): Promise<CaptureSession> => {
    const response = await apiClient.post(`/api/v1/runs/${runId}/capture-sessions`, data, { params })
    return response.data
  },

  stop: async (runId: string, sessionId: string): Promise<CaptureSession> => {
    const response = await apiClient.post(`/api/v1/runs/${runId}/capture-sessions/${sessionId}/stop`)
    return response.data
  },

  delete: async (runId: string, sessionId: string): Promise<void> => {
    await apiClient.delete(`/api/v1/runs/${runId}/capture-sessions/${sessionId}`)
  },
}

// Telemetry API
export const telemetryApi = {
  ingest: async (data: TelemetryIngest, sensorToken: string): Promise<TelemetryIngestResponse> => {
    // Для телеметрии используется прямой запрос к Experiment Service с токеном датчика
    // (не через Auth Proxy, так как это публичный endpoint)
    const EXPERIMENT_SERVICE_URL = import.meta.env.VITE_EXPERIMENT_SERVICE_URL || 'http://localhost:8002'
    const requestId = generateRequestId()
    const traceId = getTraceId()

    const response = await axios.post(
      `${EXPERIMENT_SERVICE_URL}/api/v1/telemetry`,
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

