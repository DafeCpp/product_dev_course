import axios, { type AxiosInstance } from 'axios'
import { maybeEmitHttpErrorToastFromAxiosError } from '../../utils/httpDebug'
import { AUTH_PROXY_URL } from './baseUrl'

/** Application-owned session recovery, shared by legacy and migrated domains. */
export function attachSessionInterceptors(client: AxiosInstance): void {
  // Session recovery sees Axios errors before shared transport normalization.
  client.interceptors.response.use(
    (response) => {
      return response
    },
    async (error) => {
      const originalRequest = error.config

      // Если получили 401 и это не повторный запрос.
      // Флаг `_skipAuthInterceptor` — для запросов с нестандартной авторизацией
      // (например, отправка телеметрии по sensor-токену): 401 там говорит о
      // неверном sensor-токене, а не о протухшей user-сессии, поэтому
      // refresh/logout делать не нужно.
      if (
        error.response?.status === 401 &&
        originalRequest &&
        !originalRequest._retry &&
        !originalRequest._skipAuthInterceptor
      ) {
        originalRequest._retry = true

        try {
          // Пытаемся обновить токен через Auth Proxy
          await axios.post(
            `${AUTH_PROXY_URL}/auth/refresh`,
            {},
            { withCredentials: true }
          )

          // Повторяем оригинальный запрос
          return client(originalRequest)
        } catch (refreshError) {
          // Show debug toast for refresh failure as well (dev-only), then redirect.
          maybeEmitHttpErrorToastFromAxiosError(refreshError)
          // Если refresh не удался - перенаправляем на страницу входа
          window.location.href = '/login'
          return Promise.reject(refreshError)
        }
      }

      // Emit debug toast for any request failure (dev-only; includes network/CORS/timeout).
      maybeEmitHttpErrorToastFromAxiosError(error)

      return Promise.reject(error)
    }
  )

}
