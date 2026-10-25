import { afterEach, describe, expect, test, vi } from 'vitest'
import { parseConfig } from '../src/config'

const ENV_KEYS = [
    'PORT',
    'TARGET_EXPERIMENT_URL',
    'TARGET_TELEMETRY_URL',
    'TARGET_CONFIG_SERVICE_URL',
    'TARGET_SCRIPT_URL',
    'AUTH_URL',
    'CORS_ORIGINS',
    'COOKIE_DOMAIN',
    'COOKIE_SECURE',
    'COOKIE_SAMESITE',
    'ACCESS_COOKIE_NAME',
    'REFRESH_COOKIE_NAME',
    'ACCESS_TTL_SEC',
    'REFRESH_TTL_SEC',
    'RATE_LIMIT_WINDOW_MS',
    'RATE_LIMIT_MAX',
    'LOG_LEVEL',
    'REDIS_URL',
]

function clearEnv() {
    for (const key of ENV_KEYS) vi.stubEnv(key, undefined as unknown as string)
}

afterEach(() => {
    vi.unstubAllEnvs()
})

describe('parseConfig', () => {
    test('uses docker-friendly defaults when nothing is configured', () => {
        clearEnv()

        const config = parseConfig()

        expect(config).toMatchObject({
            port: 8080,
            targetExperimentUrl: 'http://localhost:8002',
            targetTelemetryUrl: 'http://telemetry-ingest-service:8003',
            targetConfigServiceUrl: 'http://config-service:8005',
            targetScriptUrl: 'http://script-service:8004',
            authUrl: 'http://localhost:8001',
            corsOrigins: ['http://localhost:3000'],
            cookieSecure: false,
            cookieSameSite: 'lax',
            accessCookieName: 'access_token',
            refreshCookieName: 'refresh_token',
            accessTtlSec: 900,
            refreshTtlSec: 1_209_600,
            rateLimitWindowMs: 60_000,
            rateLimitMax: 60,
            logLevel: 'info',
        })
        expect(config.cookieDomain).toBeUndefined()
        expect(config.redisUrl).toBeUndefined()
    })

    test('reads every setting from the environment', () => {
        clearEnv()
        vi.stubEnv('PORT', '9090')
        vi.stubEnv('TARGET_EXPERIMENT_URL', 'http://exp')
        vi.stubEnv('TARGET_TELEMETRY_URL', 'http://tel')
        vi.stubEnv('TARGET_CONFIG_SERVICE_URL', 'http://cfg')
        vi.stubEnv('TARGET_SCRIPT_URL', 'http://scr')
        vi.stubEnv('AUTH_URL', 'http://auth')
        vi.stubEnv('CORS_ORIGINS', ' https://a.example , ,https://b.example')
        vi.stubEnv('COOKIE_DOMAIN', '.example.com')
        vi.stubEnv('COOKIE_SECURE', 'YES')
        vi.stubEnv('COOKIE_SAMESITE', 'none')
        vi.stubEnv('ACCESS_COOKIE_NAME', 'at')
        vi.stubEnv('REFRESH_COOKIE_NAME', 'rt')
        vi.stubEnv('ACCESS_TTL_SEC', '60')
        vi.stubEnv('REFRESH_TTL_SEC', '120')
        vi.stubEnv('RATE_LIMIT_WINDOW_MS', '1000')
        vi.stubEnv('RATE_LIMIT_MAX', '5')
        vi.stubEnv('LOG_LEVEL', 'debug')
        vi.stubEnv('REDIS_URL', 'redis://cache:6379')

        expect(parseConfig()).toEqual({
            port: 9090,
            targetExperimentUrl: 'http://exp',
            targetTelemetryUrl: 'http://tel',
            targetConfigServiceUrl: 'http://cfg',
            targetScriptUrl: 'http://scr',
            authUrl: 'http://auth',
            corsOrigins: ['https://a.example', 'https://b.example'],
            cookieDomain: '.example.com',
            cookieSecure: true,
            cookieSameSite: 'none',
            accessCookieName: 'at',
            refreshCookieName: 'rt',
            accessTtlSec: 60,
            refreshTtlSec: 120,
            rateLimitWindowMs: 1000,
            rateLimitMax: 5,
            logLevel: 'debug',
            redisUrl: 'redis://cache:6379',
        })
    })

    test('falls back to defaults for non-numeric numbers and treats unknown booleans as false', () => {
        clearEnv()
        vi.stubEnv('PORT', 'not-a-number')
        vi.stubEnv('RATE_LIMIT_MAX', '')
        vi.stubEnv('COOKIE_SECURE', 'nope')

        const config = parseConfig()

        expect(config.port).toBe(8080)
        expect(config.rateLimitMax).toBe(60)
        expect(config.cookieSecure).toBe(false)
    })
})
