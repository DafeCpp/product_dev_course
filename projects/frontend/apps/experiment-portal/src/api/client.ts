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
} from '../types'

// API работает через Auth Proxy, который автоматически добавляет токен из куки
const AUTH_PROXY_URL = import.meta.env.VITE_AUTH_PROXY_URL || 'http://localhost:8080'

const apiClient = axios.create({
  baseURL: AUTH_PROXY_URL,
  headers: {
    'Content-Type': 'application/json',
  },
  withCredentials: true, // Важно для работы с HttpOnly куками
})

// Обработка ошибок и автоматическое обновление токена
apiClient.interceptors.response.use(
  (response) => response,
  async (error) => {
    const originalRequest = error.config

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
    const response = await apiClient.patch(`/api/v1/runs/${id}`, { status: 'completed' })
    return response.data
  },

  fail: async (id: string, reason?: string): Promise<Run> => {
    const response = await apiClient.patch(`/api/v1/runs/${id}`, { status: 'failed', reason })
    return response.data
  },
}

