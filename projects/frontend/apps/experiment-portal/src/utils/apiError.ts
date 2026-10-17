import { HttpError } from '@lostpointer/web-http'

const MAX_MESSAGE_LENGTH = 300

function cleanMessage(value: unknown): string | null {
    if (typeof value !== 'string') return null
    // Strip control characters and collapse whitespace so a server message
    // (stack trace, HTML error page) never blows up the inline error block.
    // eslint-disable-next-line no-control-regex
    const text = value.replace(/[\u0000-\u001f\u007f]+/g, ' ').replace(/\s+/g, ' ').trim()
    if (!text) return null
    if (/^<!doctype|^<html/i.test(text)) return null
    return text.length > MAX_MESSAGE_LENGTH ? `${text.slice(0, MAX_MESSAGE_LENGTH - 1)}…` : text
}

/**
 * User-facing message for a failed API call: `response.data.error`,
 * then `response.data.message`, then `error.message`, then `fallback`.
 * Only non-empty strings are accepted; anything else falls through.
 * Axios' own `message` ("Request failed with status code 500") is skipped
 * in favour of the caller's fallback; `serverOnly` skips `error.message`
 * for any error, so client-side exceptions never reach the user.
 * A shared-transport `HttpError` carries server text in `message` only
 * when `kind === 'api'`; other kinds (network, timeout) use the fallback.
 */
export function getApiErrorMessage(
    error: unknown,
    fallback: string,
    { serverOnly = false }: { serverOnly?: boolean } = {},
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
            (serverOnly || e.isAxiosError ? null : cleanMessage(e.message)) ??
            fallback
        )
    }
    return fallback
}
