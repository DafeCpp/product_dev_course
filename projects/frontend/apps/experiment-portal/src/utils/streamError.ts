import { UserFacingError, getApiErrorMessage, responseBodyError } from './apiError'

const STREAM_FALLBACK = 'Ошибка подключения к стриму'

/** Error for a non-OK stream response, with a sanitized message from its body text. */
export function streamBodyError(text: string, status: number): UserFacingError {
    return responseBodyError(text, status, 'Ошибка стрима')
}

/** Same as `streamBodyError`, reading the body from the response. */
export async function streamResponseError(response: Response): Promise<UserFacingError> {
    const text = await response.text().catch(() => '')
    return streamBodyError(text, response.status)
}

/**
 * User-facing text for a failed telemetry stream: the opener's sanitized
 * HTTP error; anything else (fetch TypeError, the hook's own messages, the
 * server's SSE `error` event, which only ever says "stream error") gets the fallback.
 */
export function getStreamErrorMessage(err: unknown): string {
    return getApiErrorMessage(err, STREAM_FALLBACK)
}
