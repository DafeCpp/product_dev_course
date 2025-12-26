/** API клиент для аутентификации через Auth Proxy */
import axios from 'axios'
import type { User, LoginRequest, AuthResponse } from '../types'

// Auth Proxy работает на другом порту
const AUTH_PROXY_URL = import.meta.env.VITE_AUTH_PROXY_URL || 'http://localhost:8080'

const authClient = axios.create({
    baseURL: AUTH_PROXY_URL,
    headers: {
        'Content-Type': 'application/json',
    },
    withCredentials: true, // Важно для работы с HttpOnly куками
})

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

