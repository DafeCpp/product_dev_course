import type { Config } from './config'

export interface AuthRateLimits {
    authLoginRateLimit: { max: number; timeWindow: number }
    authRegisterRateLimit: { max: number; timeWindow: number }
    authMutationRateLimit: { max: number; timeWindow: number }
}

export function buildAuthRateLimits(config: Config): AuthRateLimits {
    return {
        authLoginRateLimit: { max: 10, timeWindow: config.rateLimitWindowMs },
        authRegisterRateLimit: { max: 5, timeWindow: config.rateLimitWindowMs },
        authMutationRateLimit: { max: config.rateLimitMax, timeWindow: config.rateLimitWindowMs },
    }
}
