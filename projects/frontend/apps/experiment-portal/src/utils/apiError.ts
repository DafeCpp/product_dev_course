import { HttpError } from '@lostpointer/web-http'

const MAX_MESSAGE_LENGTH = 300

function cleanMessage(value: unknown): string | null {
    if (typeof value !== 'string') return null
    // Strip control characters and collapse whitespace so a server message
    // (stack trace, HTML error page) never blows up the inline error block.
    // eslint-disable-next-line no-control-regex
    const text = value.replace(/[\u0000-\u001f\u007f]+/g, ' ').replace(/\s+/g, ' ').trim()
    if (!text) return null
    // Markup bodies (proxy error pages, XML) are never shown to the user;
    // a leading placeholder such as "<sensor_id> must be a UUID" is kept.
    if (
        /^<[!?]/.test(text) ||
        /^<(html|head|body|meta|link|title|style|script|div|span|pre|strong|img|center|font|table|form|section|main|header|footer|nav|article|h[1-6])(\s|>|\/)/i.test(text) ||
        /^<([a-z][a-z0-9]*)\b[^>]*>.*<\/\1>/i.test(text) ||
        /^<[a-z][a-z0-9]*\b[^>]*\/>$/i.test(text)
    ) return null
    if (text.length <= MAX_MESSAGE_LENGTH) return text
    const chars = Array.from(text)
    return chars.length > MAX_MESSAGE_LENGTH ? `${chars.slice(0, MAX_MESSAGE_LENGTH - 1).join('')}…` : text
}

function isServerFailure(status: number | undefined): boolean {
    return typeof status === 'number' && status >= 500
}

/** An Error whose message is already safe to show the user (e.g. built from a sanitized response body). */
export class UserFacingError extends Error {
    override name = 'UserFacingError'
}

/**
 * User-facing message for a failed API call: `response.data.message`,
 * then `response.data.error`, then a plain-text `response.data`, then `fallback`. Same order as the shared
 * transport: backends put a code in `error` and human text in `message`.
 * Only non-empty strings are accepted; anything else falls through.
 * Body text of a 5xx response is internal (proxy/stack details) and never shown.
 * A shared-transport `HttpError` carries server text in `message` only
 * when `kind === 'api'` and the body had one (not the synthetic `HTTP 500`);
 * other kinds (network, timeout) use the fallback. A `UserFacingError`
 * always shows its message. `includeClientMessage` also accepts `error.message` of a plain Error;
 * off by default so client-side exceptions never reach the user. Axios'
 * own message ("Request failed with status code 500") is always skipped.
 */
export function getApiErrorMessage(
    error: unknown,
    fallback: string,
    { includeClientMessage = false }: { includeClientMessage?: boolean } = {},
): string {
    if (error instanceof UserFacingError) {
        return cleanMessage(error.message) ?? fallback
    }
    if (error instanceof HttpError) {
        const synthetic = error.message === `HTTP ${error.status}`
        const usable = error.kind === 'api' && !synthetic && !isServerFailure(error.status)
        return (usable ? cleanMessage(error.message) : null) ?? fallback
    }
    if (error && typeof error === 'object') {
        const e = error as {
            response?: { data?: unknown; status?: number }
            message?: unknown
            isAxiosError?: boolean
        }
        const data = isServerFailure(e.response?.status) ? undefined : e.response?.data
        const body = data && typeof data === 'object' ? (data as { error?: unknown; message?: unknown }) : undefined
        return (
            cleanMessage(body?.message) ??
            cleanMessage(body?.error) ??
            // `responseType: 'text'` requests get a JSON body as an unparsed string.
            (typeof data === 'string' ? bodyTextMessage(data) : null) ??
            (includeClientMessage && !e.isAxiosError ? cleanMessage(e.message) : null) ??
            fallback
        )
    }
    return fallback
}

/**
 * User-facing message from a raw response body: the `message` / `error`
 * string of a JSON body, else plain body text, else `fallback`. A JSON
 * body without such a string never shows raw JSON.
 */
export function getResponseBodyMessage(body: string, fallback: string): string {
    return bodyTextMessage(body) ?? fallback
}

function bodyTextMessage(body: string): string | null {
    let parsed: unknown
    try {
        parsed = JSON.parse(body)
    } catch {
        return cleanMessage(body)
    }
    if (typeof parsed === 'string') return cleanMessage(parsed)
    if (parsed && typeof parsed === 'object') {
        const p = parsed as { error?: unknown; message?: unknown }
        return cleanMessage(p.message) ?? cleanMessage(p.error)
    }
    return null
}

/** HTTP status of a failed request (shared transport or axios), if it got a response. */
export function getErrorStatus(error: unknown): number | undefined {
    if (error instanceof HttpError) return error.kind === 'api' ? error.status : undefined
    if (error && typeof error === 'object') {
        const status = (error as { response?: { status?: unknown } }).response?.status
        return typeof status === 'number' ? status : undefined
    }
    return undefined
}

/** True for a 404 response. */
export function isNotFoundError(error: unknown): boolean {
    return getErrorStatus(error) === 404
}

/** True for a 4xx response: the request itself was rejected, the service is up. */
export function isClientRequestError(error: unknown): boolean {
    const status = getErrorStatus(error)
    return status !== undefined && status >= 400 && status < 500
}

/** Error for a non-OK fetch response: sanitized 4xx body text, else `${prefix}: HTTP ${status}`. */
export function responseBodyError(body: string, status: number, prefix: string): UserFacingError {
    const fallback = `${prefix}: HTTP ${status}`
    return new UserFacingError(isServerFailure(status) ? fallback : getResponseBodyMessage(body, fallback))
}
