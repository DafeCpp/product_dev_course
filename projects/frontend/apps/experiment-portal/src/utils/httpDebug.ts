import type { AxiosError, AxiosRequestConfig, AxiosResponse } from 'axios'
import { emitToast } from './toastBus'
import { IS_TEST } from './env'

export type HttpDebugHeaders = Record<string, string>

export type HttpDebugRequest = {
  method?: string
  url?: string
  baseURL?: string
  params?: unknown
  headers?: HttpDebugHeaders
  body?: unknown
}

export type HttpDebugResponse = {
  status?: number
  statusText?: string
  headers?: HttpDebugHeaders
  body?: unknown
}

export type HttpDebugCorrelation = {
  trace_id?: string
  request_id?: string
  traceparent?: string
  x_trace_id?: string
  x_request_id?: string
}

export type HttpDebugInfo = {
  kind: 'http'
  message: string
  request: HttpDebugRequest
  response?: HttpDebugResponse
  correlation?: HttpDebugCorrelation
}

const MASK = '[REDACTED]'
const DEFAULT_TRUNCATE_CHARS = 8_192

const SENSITIVE_HEADER_KEYS = new Set([
  'authorization',
  'cookie',
  'set-cookie',
  'x-csrf-token',
  'x-api-key',
  'x-auth-token',
])

const SENSITIVE_BODY_KEYS = [
  'password',
  'pass',
  'pwd',
  'token',
  'access_token',
  'refresh_token',
  'secret',
  'api_key',
  'apikey',
  'session',
  'cookie',
]

export function truncateString(s: string, maxChars = DEFAULT_TRUNCATE_CHARS): string {
  if (s.length <= maxChars) return s
  return `${s.slice(0, maxChars)}\n…[truncated ${s.length - maxChars} chars]…`
}

function _toHeaderRecord(input: unknown): Record<string, string> {
  const out: Record<string, string> = {}
  if (!input || typeof input !== 'object') return out

  // Axios may provide headers in various shapes; we only support simple key/value projection.
  for (const [k, v] of Object.entries(input as any)) {
    if (v === undefined || v === null) continue
    if (typeof v === 'string') out[k] = v
    else if (typeof v === 'number' || typeof v === 'boolean') out[k] = String(v)
    else if (Array.isArray(v)) out[k] = v.map(String).join(', ')
    else out[k] = String(v)
  }
  return out
}

export function sanitizeHeaders(headers: unknown): HttpDebugHeaders {
  const raw = _toHeaderRecord(headers)
  const out: Record<string, string> = {}

  for (const [k, v] of Object.entries(raw)) {
    const keyLower = k.toLowerCase()
    if (SENSITIVE_HEADER_KEYS.has(keyLower)) {
      out[k] = MASK
      continue
    }
    // Heuristic: if header name contains token/secret, mask.
    if (keyLower.includes('token') || keyLower.includes('secret')) {
      out[k] = MASK
      continue
    }
    out[k] = truncateString(v)
  }

  return out
}

function _isPlainObject(x: any): x is Record<string, any> {
  return typeof x === 'object' && x !== null && !Array.isArray(x)
}

export function sanitizeBody(value: unknown, depth = 0): unknown {
  if (depth > 6) return '[MaxDepth]'
  if (value === null || value === undefined) return value

  if (typeof value === 'string') return truncateString(value)
  if (typeof value === 'number' || typeof value === 'boolean') return value

  if (Array.isArray(value)) {
    return value.slice(0, 200).map((v) => sanitizeBody(v, depth + 1))
  }

  if (_isPlainObject(value)) {
    const out: Record<string, unknown> = {}
    for (const [k, v] of Object.entries(value)) {
      const keyLower = k.toLowerCase()
      const isSensitive =
        SENSITIVE_BODY_KEYS.includes(keyLower) ||
        keyLower.includes('token') ||
        keyLower.includes('secret') ||
        keyLower.includes('password')
      out[k] = isSensitive ? MASK : sanitizeBody(v, depth + 1)
    }
    return out
  }

  try {
    return truncateString(String(value))
  } catch {
    return '[Unserializable]'
  }
}

function _pretty(value: unknown): string {
  if (value === undefined) return ''
  if (typeof value === 'string') return truncateString(value)
  try {
    return truncateString(JSON.stringify(value, null, 2))
  } catch {
    return truncateString(String(value))
  }
}

export function formatHttpDebugText(info: HttpDebugInfo): string {
  const lines: string[] = []

  lines.push(`Message: ${info.message}`)
  lines.push('')

  lines.push('Request:')
  lines.push(`  method: ${info.request.method || ''}`)
  lines.push(`  url: ${info.request.url || ''}`)
  if (info.request.baseURL) lines.push(`  baseURL: ${info.request.baseURL}`)
  if (info.request.params !== undefined) lines.push(`  params: ${_pretty(info.request.params)}`)
  if (info.request.headers && Object.keys(info.request.headers).length) {
    lines.push(`  headers: ${_pretty(info.request.headers)}`)
  }
  if (info.request.body !== undefined) lines.push(`  body: ${_pretty(info.request.body)}`)
  lines.push('')

  if (info.response) {
    lines.push('Response:')
    if (typeof info.response.status === 'number') lines.push(`  status: ${info.response.status}`)
    if (info.response.statusText) lines.push(`  statusText: ${info.response.statusText}`)
    if (info.response.headers && Object.keys(info.response.headers).length) {
      lines.push(`  headers: ${_pretty(info.response.headers)}`)
    }
    if (info.response.body !== undefined) lines.push(`  body: ${_pretty(info.response.body)}`)
    lines.push('')
  }

  if (info.correlation) {
    lines.push('Correlation:')
    if (info.correlation.trace_id) lines.push(`  trace_id: ${info.correlation.trace_id}`)
    if (info.correlation.request_id) lines.push(`  request_id: ${info.correlation.request_id}`)
    if (info.correlation.traceparent) lines.push(`  traceparent: ${info.correlation.traceparent}`)
    if (info.correlation.x_trace_id) lines.push(`  x-trace-id: ${info.correlation.x_trace_id}`)
    if (info.correlation.x_request_id) lines.push(`  x-request-id: ${info.correlation.x_request_id}`)
  }

  return lines.join('\n').trimEnd()
}

function _dedupeKey(info: HttpDebugInfo): string {
  const status = info.response?.status ?? ''
  const method = info.request.method ?? ''
  const url = info.request.url ?? ''
  return `${status}|${method}|${url}|${info.message}`
}

const _dedupeSeenAt = new Map<string, number>()
const DEDUPE_WINDOW_MS = 3000

export function shouldEmitDebugToast(info: HttpDebugInfo): boolean {
  const key = _dedupeKey(info)
  const now = Date.now()
  const prev = _dedupeSeenAt.get(key)
  if (typeof prev === 'number' && now - prev < DEDUPE_WINDOW_MS) return false
  _dedupeSeenAt.set(key, now)
  return true
}

function _extractCorrelationFromHeaders(reqHeaders?: HttpDebugHeaders, respHeaders?: HttpDebugHeaders): HttpDebugCorrelation {
  const req = reqHeaders || {}
  const resp = respHeaders || {}

  const getAny = (h: HttpDebugHeaders, keys: string[]): string | undefined => {
    for (const k of keys) {
      const direct = h[k]
      if (direct) return direct
      const found = Object.entries(h).find(([kk]) => kk.toLowerCase() === k.toLowerCase())
      if (found?.[1]) return found[1]
    }
    return undefined
  }

  return {
    trace_id: getAny(req, ['X-Trace-Id', 'x-trace-id']),
    request_id: getAny(req, ['X-Request-Id', 'x-request-id']),
    traceparent: getAny(resp, ['traceparent']) || getAny(req, ['traceparent']),
    x_trace_id: getAny(resp, ['x-trace-id']),
    x_request_id: getAny(resp, ['x-request-id']),
  }
}

function _resolveUrl(cfg?: AxiosRequestConfig): string | undefined {
  const url = cfg?.url
  if (!url) return undefined
  const base = cfg?.baseURL
  if (!base) return url
  // If it's already absolute, keep it.
  if (/^https?:\/\//i.test(url)) return url
  return `${base.replace(/\/+$/, '')}/${url.replace(/^\/+/, '')}`
}

export function buildHttpDebugInfoFromAxiosError(error: unknown): HttpDebugInfo {
  const e = error as AxiosError
  const cfg = e?.config as AxiosRequestConfig | undefined
  const resp = e?.response as AxiosResponse | undefined

  const reqHeaders = sanitizeHeaders(cfg?.headers)
  const respHeaders = sanitizeHeaders(resp?.headers)

  const message =
    e?.message ||
    (resp?.status ? `HTTP ${resp.status}` : 'Request failed')

  return {
    kind: 'http',
    message,
    request: {
      method: cfg?.method?.toUpperCase(),
      url: _resolveUrl(cfg),
      baseURL: cfg?.baseURL,
      params: sanitizeBody(cfg?.params),
      headers: reqHeaders,
      body: sanitizeBody((cfg as any)?.data),
    },
    response: resp
      ? {
        status: resp.status,
        statusText: resp.statusText,
        headers: respHeaders,
        body: sanitizeBody(resp.data),
      }
      : undefined,
    correlation: _extractCorrelationFromHeaders(reqHeaders, respHeaders),
  }
}

export function buildHttpDebugInfoFromFetch(args: {
  message: string
  request: { method?: string; url?: string; headers?: unknown; body?: unknown }
  response?: { status?: number; statusText?: string; headers?: unknown; body?: unknown }
}): HttpDebugInfo {
  const reqHeaders = sanitizeHeaders(args.request.headers)
  const respHeaders = sanitizeHeaders(args.response?.headers)
  return {
    kind: 'http',
    message: args.message,
    request: {
      method: args.request.method,
      url: args.request.url,
      headers: reqHeaders,
      body: sanitizeBody(args.request.body),
    },
    response: args.response
      ? {
        status: args.response.status,
        statusText: args.response.statusText,
        headers: respHeaders,
        body: sanitizeBody(args.response.body),
      }
      : undefined,
    correlation: _extractCorrelationFromHeaders(reqHeaders, respHeaders),
  }
}

export function maybeEmitHttpErrorToast(info: HttpDebugInfo): void {
  if (IS_TEST) return
  if (!shouldEmitDebugToast(info)) return

  // Один тост на все окружения: заголовок — кратко, тело — DebugErrorToast (Details + Copy).
  // Данные уже санитизированы (маскировка секретов), безопасно показывать в prod.
  const status = typeof info.response?.status === 'number' ? `HTTP ${info.response?.status}` : 'Ошибка сети'
  const method = info.request.method || ''
  const url = info.request.url || ''
  const traceId = info.correlation?.trace_id || info.correlation?.x_trace_id
  const requestId = info.correlation?.request_id || info.correlation?.x_request_id

  let shortUrl = url
  try {
    const u = new URL(url)
    shortUrl = `${u.pathname}${u.hash || ''}`
  } catch {
    shortUrl = String(url).split('?')[0] || String(url)
  }

  const idsLine = [traceId ? `trace_id=${traceId}` : null, requestId ? `request_id=${requestId}` : null]
    .filter(Boolean)
    .join(' ')

  const title = 'Ошибка запроса'
  const message = [
    `${status}${method || shortUrl ? ` — ${method} ${shortUrl}` : ''}`.trim(),
    idsLine,
  ]
    .filter(Boolean)
    .join('\n')

  emitToast({
    kind: 'debug-http-error',
    variant: 'error',
    title,
    message,
    payload: info,
    durationMs: 10000,
  })
}

export function maybeEmitHttpErrorToastFromAxiosError(error: unknown): void {
  const info = buildHttpDebugInfoFromAxiosError(error)
  maybeEmitHttpErrorToast(info)
}

