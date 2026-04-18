/** API клиент для аутентификации через Auth Proxy */
import axios from 'axios'
import type {
    User,
    AdminUser,
    AdminInviteToken,
    LoginRequest,
    RegisterRequest,
    AuthResponse,
} from '../types'
import { generateRequestId } from '../utils/uuid'
import { getTraceId } from '../utils/trace'
import { getCsrfToken } from '../utils/csrf'
import { maybeEmitHttpErrorToastFromAxiosError } from '../utils/httpDebug'

// Auth Proxy работает на другом порту
const AUTH_PROXY_URL = import.meta.env.VITE_AUTH_PROXY_URL ?? 'http://localhost:8080'

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

    // --- Admin API ---

    adminListUsers: async (params?: { search?: string; is_active?: boolean }): Promise<AdminUser[]> => {
        const query = new URLSearchParams()
        if (params?.search) query.set('search', params.search)
        if (params?.is_active !== undefined) query.set('is_active', String(params.is_active))
        const qs = query.toString()
        const response = await authClient.get<AdminUser[]>(`/auth/admin/users${qs ? `?${qs}` : ''}`)
        return response.data
    },

    adminUpdateUser: async (
        userId: string,
        data: { is_active?: boolean; is_admin?: boolean }
    ): Promise<AdminUser> => {
        const response = await authClient.patch<AdminUser>(`/auth/admin/users/${userId}`, data)
        return response.data
    },

    adminDeleteUser: async (userId: string): Promise<void> => {
        await authClient.delete(`/auth/admin/users/${userId}`)
    },

    adminResetUserPassword: async (
        userId: string,
        newPassword?: string
    ): Promise<{ user: AdminUser; new_password: string }> => {
        const response = await authClient.post<{ user: AdminUser; new_password: string }>(
            `/auth/admin/users/${userId}/reset`,
            newPassword ? { new_password: newPassword } : {}
        )
        return response.data
    },

    adminCreateInvite: async (data: {
        email_hint?: string
        expires_in_hours?: number
    }): Promise<AdminInviteToken> => {
        const response = await authClient.post<AdminInviteToken>('/auth/admin/invites', {
            expires_in_hours: data.expires_in_hours ?? 72,
            ...(data.email_hint ? { email_hint: data.email_hint } : {}),
        })
        return response.data
    },

    adminListInvites: async (activeOnly?: boolean): Promise<AdminInviteToken[]> => {
        const qs = activeOnly ? '?active_only=true' : ''
        const response = await authClient.get<AdminInviteToken[]>(`/auth/admin/invites${qs}`)
        return response.data
    },

    adminRevokeInvite: async (token: string): Promise<void> => {
        await authClient.delete(`/auth/admin/invites/${token}`)
    },

    /**
     * Смена пароля пользователя
     */
    changePassword: async (data: {
        old_password: string
        new_password: string
    }): Promise<void> => {
        await authClient.post('/auth/change-password', data)
    },

    /**
     * Запрос сброса пароля — отправляет письмо на email
     */
    requestPasswordReset: async (email: string): Promise<{ message: string }> => {
        const response = await authClient.post<{ message: string }>(
            '/auth/password-reset/request',
            { email }
        )
        return response.data
    },

    /**
     * Подтверждение сброса пароля по токену из письма
     */
    confirmPasswordReset: async (
        token: string,
        newPassword: string
    ): Promise<AuthResponse> => {
        const response = await authClient.post<AuthResponse>(
            '/auth/password-reset/confirm',
            { reset_token: token, new_password: newPassword }
        )
        return response.data
    },
}

