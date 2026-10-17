import { describe, it, expect } from 'vitest'
import { UserFacingError } from './apiError'
import { getStreamErrorMessage, streamResponseError } from './streamError'

describe('getStreamErrorMessage', () => {
    it('shows a sanitized opener error', () => {
        expect(getStreamErrorMessage(new UserFacingError('Ошибка стрима: HTTP 404'))).toBe('Ошибка стрима: HTTP 404')
    })

    it('hides browser and hook-internal errors', () => {
        expect(getStreamErrorMessage(new TypeError('Failed to fetch'))).toBe('Ошибка подключения к стриму')
        expect(getStreamErrorMessage(new Error('Telemetry stream ended'))).toBe('Ошибка подключения к стриму')
        expect(getStreamErrorMessage(new Error('stream error'))).toBe('Ошибка подключения к стриму')
    })
})

describe('streamResponseError', () => {
    it('builds a sanitized error from the response body', async () => {
        const json = await streamResponseError(new Response('{"error":"sensor not found"}', { status: 404 }))
        expect(json.message).toBe('sensor not found')
        const html = await streamResponseError(new Response('<html><body>502</body></html>', { status: 502 }))
        expect(html.message).toBe('Ошибка стрима: HTTP 502')
    })
})
