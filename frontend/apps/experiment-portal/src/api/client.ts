/** API клиент для взаимодействия с бэкендом */
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

const API_BASE_URL = import.meta.env.VITE_API_URL || 'http://localhost:8002'

const apiClient = axios.create({
  baseURL: API_BASE_URL,
  headers: {
    'Content-Type': 'application/json',
  },
})

// Добавление токена в заголовки
apiClient.interceptors.request.use((config) => {
  const token = localStorage.getItem('access_token')
  if (token) {
    config.headers.Authorization = `Bearer ${token}`
  }
  return config
})

// Обработка ошибок
apiClient.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error.response?.status === 401) {
      // Перенаправление на страницу входа
      localStorage.removeItem('access_token')
      window.location.href = '/login'
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

