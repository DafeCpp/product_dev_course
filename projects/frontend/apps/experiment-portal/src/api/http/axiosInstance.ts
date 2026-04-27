import axios from 'axios'
import type { AxiosInstance } from 'axios'
import { generateRequestId } from '../../utils/uuid'
import { getTraceId } from '../../utils/trace'
import { getCsrfToken } from '../../utils/csrf'
import { maybeEmitHttpErrorToastFromAxiosError } from '../../utils/httpDebug'
import { AUTH_PROXY_URL } from './baseUrl'

interface CreateClientOptions {
  timeout?: number
  /** Skip automatic debug toast on response errors (use when the caller adds its own response interceptor). */
  skipDebugToast?: boolean
}

const STATE_CHANGING_METHODS = new Set(['POST', 'PUT', 'PATCH', 'DELETE'])

/**
 * Creates an axios instance preconfigured for Auth Proxy:
 * - X-Trace-Id / X-Request-Id on every request
 * - X-CSRF-Token on state-changing methods
 * - DEV-only error toast on response failure
 *
 * Does NOT include 401-refresh — callers that need it should add their own
 * response interceptor on top.
 */
export function createAuthProxyClient(options: CreateClientOptions = {}): AxiosInstance {
  const instance = axios.create({
    baseURL: AUTH_PROXY_URL,
    headers: { 'Content-Type': 'application/json' },
    withCredentials: true,
    ...(options.timeout !== undefined ? { timeout: options.timeout } : {}),
  })

  instance.interceptors.request.use((config) => {
    config.headers['X-Trace-Id'] = getTraceId()
    config.headers['X-Request-Id'] = generateRequestId()

    const method = (config.method ?? 'get').toUpperCase()
    if (STATE_CHANGING_METHODS.has(method)) {
      const csrf = getCsrfToken()
      if (csrf) config.headers['X-CSRF-Token'] = csrf
    }

    return config
  })

  if (!options.skipDebugToast) {
    instance.interceptors.response.use(
      (response) => response,
      (error) => {
        maybeEmitHttpErrorToastFromAxiosError(error)
        return Promise.reject(error)
      },
    )
  }

  return instance
}
