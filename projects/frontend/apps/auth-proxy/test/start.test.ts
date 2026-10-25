import { afterEach, describe, expect, test, vi } from 'vitest'
import { start } from '../src/index'

afterEach(() => {
    vi.unstubAllEnvs()
    vi.restoreAllMocks()
})

describe('start', () => {
    test('listens on the configured port and serves health', async () => {
        vi.stubEnv('PORT', '0')
        vi.stubEnv('LOG_LEVEL', 'silent')
        vi.stubEnv('REDIS_URL', '')

        const app = await start()

        try {
            const res = await app.inject({ method: 'GET', url: '/health' })
            expect(res.json()).toEqual({ status: 'ok' })
            expect(app.server.listening).toBe(true)
        } finally {
            await app.close()
        }
    })

    test('exits with code 1 when the port is already taken', async () => {
        vi.stubEnv('LOG_LEVEL', 'silent')
        vi.stubEnv('REDIS_URL', '')
        vi.stubEnv('PORT', '0')
        const first = await start()
        const address = first.server.address()
        const port = typeof address === 'object' && address ? address.port : 0
        vi.stubEnv('PORT', String(port))
        const exit = vi.spyOn(process, 'exit').mockImplementation((() => undefined) as never)

        try {
            const second = await start()
            expect(exit).toHaveBeenCalledWith(1)
            await second.close()
        } finally {
            await first.close()
        }
    })
})
