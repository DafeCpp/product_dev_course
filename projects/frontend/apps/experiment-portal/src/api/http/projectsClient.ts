import { createHttpClient } from '@lostpointer/web-http'
import { generateRequestId } from '../../utils/uuid'
import { getTraceId } from '../../utils/trace'
import { getCsrfToken } from '../../utils/csrf'
import { AUTH_PROXY_URL } from './baseUrl'
import { attachSessionInterceptors } from './session'

/** P10-09 pilot: only Projects uses the shared transport in this release. */
export const projectsClient = createHttpClient({
  baseURL: AUTH_PROXY_URL,
  timeoutMs: 30_000,
  headers: { 'Content-Type': 'application/json' },
  requestId: generateRequestId,
  csrfToken: getCsrfToken,
  configure(client) {
    client.interceptors.request.use((config) => {
      config.headers.set('X-Trace-Id', getTraceId())
      return config
    })
    attachSessionInterceptors(client)
  },
})
