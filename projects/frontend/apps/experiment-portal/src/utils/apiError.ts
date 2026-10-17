import { HttpError } from '@lostpointer/web-http'

const MAX_MESSAGE_LENGTH = 300

function cleanMessage(value: unknown): string | null {
    if (typeof value !== 'string') return null
    // Strip control characters and collapse whitespace so a server message
    // (stack trace, HTML error page) never blows up the inline error block.
    // eslint-disable-next-line no-control-regex
    const text = value.replace(/[\u0000-\u001f\u007f]+/g, ' ').replace(/\s+/g, ' ').trim()
    if (!text) return null
    // Markup bodies (proxy error pages, XML) are never shown to the user.
    if (/^<[a-z!?/]/i.test(text)) return null
    const chars = Array.from(text)
    return chars.length > MAX_MESSAGE_LENGTH ? `${chars.slice(0, MAX_MESSAGE_LENGTH - 1).join('')}…` : text
}

/**
 * User-facing message for a failed API call: `response.data.error`,
 * then `response.data.message`, then `fallback`.
 * Only non-empty strings are accepted; anything else falls through.
 * A shared-transport `HttpError` carries server text in `message` only
 * when `kind === 'api'`; other kinds (network, timeout) use the fallback.
 * `includeClientMessage` also accepts `error.message` of a plain Error;
 * off by default so client-side exceptions never reach the user. Axios'
 * own message ("Request failed with status code 500") is always skipped.
 */
export function getApiErrorMessage(
    error: unknown,
    fallback: string,
    { includeClientMessage = false }: { includeClientMessage?: boolean } = {},
): string {
    if (error instanceof HttpError) {
        return (error.kind === 'api' ? cleanMessage(error.message) : null) ?? fallback
    }
    if (error && typeof error === 'object') {
        const e = error as {
            response?: { data?: { error?: unknown; message?: unknown } }
            message?: unknown
            isAxiosError?: boolean
        }
        return (
            cleanMessage(e.response?.data?.error) ??
            cleanMessage(e.response?.data?.message) ??
            (includeClientMessage && !e.isAxiosError ? cleanMessage(e.message) : null) ??
            fallback
        )
    }
    return fallback
}

/**
 * User-facing message from a raw response body: the `error` / `message`
 * string of a JSON body, else the body text itself, else `fallback`.
 */
export function getResponseBodyMessage(body: string, fallback: string): string {
    try {
        const parsed: unknown = JSON.parse(body)
        if (parsed && typeof parsed === 'object') {
            const p = parsed as { error?: unknown; message?: unknown }
            const fromJson = cleanMessage(p.error) ?? cleanMessage(p.message)
            if (fromJson) return fromJson
        }
    } catch {
        // plain-text body
    }
    return cleanMessage(body) ?? fallback
}
