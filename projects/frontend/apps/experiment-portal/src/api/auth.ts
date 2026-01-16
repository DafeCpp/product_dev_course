/** API клиент для аутентификации через Auth Proxy */
import axios from 'axios'
import type { User, LoginRequest, RegisterRequest, AuthResponse } from '../types'
import { generateRequestId } from '../utils/uuid'
import { getTraceId } from '../utils/trace'
import { getCsrfToken } from '../utils/csrf'
import { maybeEmitHttpErrorToastFromAxiosError } from '../utils/httpDebug'

// Auth Proxy работает на другом порту
const AUTH_PROXY_URL = import.meta.env.VITE_AUTH_PROXY_URL || 'http://localhost:8080'

const authClient = axios.create({
    baseURL: AUTH_PROXY_URL,
    headers: {
        'Content-Type': 'application/json',
    },
    withCredentials: true, // Важно для работы с HttpOnly куками
})

// Interceptor для добавления trace_id и request_id в заголовки
authClient.interceptors.request.use(
    (config) => {
        // Генерируем request_id для каждого запроса
        const requestId = generateRequestId()
        const traceId = getTraceId()

        // Добавляем заголовки
        config.headers['X-Trace-Id'] = traceId
        config.headers['X-Request-Id'] = requestId

        // CSRF (double-submit cookie) for cookie-authenticated, state-changing requests.
        // auth-proxy excludes /auth/login and /auth/refresh, but we can still attach token safely.
        const method = (config.method || 'get').toUpperCase()
        const isStateChanging = ['POST', 'PUT', 'PATCH', 'DELETE'].includes(method)
        if (isStateChanging) {
            const csrf = getCsrfToken()
            if (csrf) {
                config.headers['X-CSRF-Token'] = csrf
            }
        }
        return config
    },
    (error) => {
        return Promise.reject(error)
    }
)

// Interceptor для логирования ошибок
authClient.interceptors.response.use(
    (response) => response,
    (error) => {
        // Emit debug toast for auth failures (dev-only).
        maybeEmitHttpErrorToastFromAxiosError(error)

        return Promise.reject(error)
    }
)

export const authApi = {
    /**
     * Вход пользователя
     * Токены сохраняются в HttpOnly куках автоматически
     */
    login: async (credentials: LoginRequest): Promise<AuthResponse> => {
        const response = await authClient.post<AuthResponse>('/auth/login', credentials)
        return response.data
    },

    /**
     * Регистрация пользователя
     */
    register: async (payload: RegisterRequest): Promise<AuthResponse> => {
        const response = await authClient.post<AuthResponse>('/auth/register', payload)
        return response.data
    },

    /**
     * Обновление токена
     * Использует refresh token из куки
     */
    refresh: async (): Promise<AuthResponse> => {
        const response = await authClient.post<AuthResponse>('/auth/refresh')
        return response.data
    },

    /**
     * Выход пользователя
     * Очищает куки
     */
    logout: async (): Promise<void> => {
        await authClient.post('/auth/logout')
    },

    /**
     * Получение профиля текущего пользователя
     * Использует access token из куки
     */
    me: async (): Promise<User> => {
        const response = await authClient.get<User>('/auth/me')
        return response.data
    },
}

