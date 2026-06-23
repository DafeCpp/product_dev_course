import { generateRequestId } from '../../utils/uuid'
import { getTraceId } from '../../utils/trace'
import {
  buildHttpDebugInfoFromFetch,
  maybeEmitHttpErrorToast,
  truncateString,
} from '../../utils/httpDebug'

export function makeFetchHeaders(): Record<string, string> {
  return {
    'X-Trace-Id': getTraceId(),
    'X-Request-Id': generateRequestId(),
  }
}

/** GET via fetch with standard trace headers, cookie credentials, and DEV error toast on failure. */
export async function apiFetch<T>(url: string): Promise<T> {
  const headers = makeFetchHeaders()
  const resp = await fetch(url, {
    method: 'GET',
    headers,
    credentials: 'include',
  })

  if (!resp.ok) {
    const text = await resp.text().catch(() => '')
    maybeEmitHttpErrorToast(
      buildHttpDebugInfoFromFetch({
        message: text || `Ошибка запроса: HTTP ${resp.status}`,
        request: { method: 'GET', url, headers },
        response: {
          status: resp.status,
          statusText: resp.statusText,
          headers: Object.fromEntries(resp.headers.entries()),
          body: truncateString(text || ''),
        },
      }),
    )
    throw new Error(text || `Ошибка запроса: HTTP ${resp.status}`)
  }

  return (await resp.json()) as T
}
