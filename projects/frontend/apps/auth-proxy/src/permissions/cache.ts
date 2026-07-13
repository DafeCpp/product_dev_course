import type { FastifyInstance } from 'fastify'
import Redis from 'ioredis'
import type { Config } from '../config'

export interface EffectivePermissions {
    user_id: string
    is_superadmin: boolean
    system_permissions: string[]
    project_permissions: string[]
}

export interface PermissionsCache {
    get(key: string): Promise<EffectivePermissions | null>
    set(key: string, value: EffectivePermissions, ttlSec: number): Promise<void>
    invalidateUser(userId: string): Promise<void>
}

class NoopPermissionsCache implements PermissionsCache {
    get = async (_key: string): Promise<EffectivePermissions | null> => null
    set = async (
        _key: string,
        _value: EffectivePermissions,
        _ttlSec: number
    ): Promise<void> => {}
    invalidateUser = async (_userId: string): Promise<void> => {}
}

export class InMemoryPermissionsCache implements PermissionsCache {
    private store = new Map<string, { value: EffectivePermissions; expiresAt: number }>()

    async get(key: string): Promise<EffectivePermissions | null> {
        const entry = this.store.get(key)
        if (!entry || Date.now() >= entry.expiresAt) return null
        return entry.value
    }

    async set(key: string, value: EffectivePermissions, ttlSec: number): Promise<void> {
        this.store.set(key, { value, expiresAt: Date.now() + ttlSec * 1000 })
    }

    async invalidateUser(userId: string): Promise<void> {
        for (const key of [...this.store.keys()]) {
            if (key.startsWith(`perms:${userId}`) || key === `perms:sys:${userId}`) {
                this.store.delete(key)
            }
        }
    }

    clear(): void {
        this.store.clear()
    }
}

class RedisPermissionsCache implements PermissionsCache {
    constructor(private readonly redis: Redis) {}

    async get(key: string): Promise<EffectivePermissions | null> {
        try {
            const raw = await this.redis.get(key)
            return raw ? (JSON.parse(raw) as EffectivePermissions) : null
        } catch {
            return null
        }
    }

    async set(key: string, value: EffectivePermissions, ttlSec: number): Promise<void> {
        try {
            await this.redis.setex(key, ttlSec, JSON.stringify(value))
        } catch {
            // Cache failures must not break authorization requests.
        }
    }

    async invalidateUser(userId: string): Promise<void> {
        try {
            const keysToDelete = [`perms:sys:${userId}`]
            const stream = this.redis.scanStream({ match: `perms:${userId}:*`, count: 100 })
            await new Promise<void>((resolve, reject) => {
                stream.on('data', (keys: string[]) => keysToDelete.push(...keys))
                stream.once('end', resolve)
                stream.once('error', reject)
            })
            await this.redis.del(...keysToDelete)
        } catch {
            // Cache failures must not break authorization requests.
        }
    }
}

export async function createPermissionsCache(
    app: FastifyInstance,
    config: Config,
    injected?: PermissionsCache
): Promise<PermissionsCache> {
    if (injected) return injected
    if (!config.redisUrl) return new NoopPermissionsCache()

    const redis = new Redis(config.redisUrl, {
        maxRetriesPerRequest: 1,
        enableReadyCheck: false,
        lazyConnect: true,
    })
    redis.on('error', (err: Error) => {
        app.log.warn({ err: err.message }, 'Redis connection error — permissions cache disabled')
    })

    try {
        await redis.connect()
        app.addHook('onClose', async () => {
            await redis.quit().catch(() => {})
        })
        return new RedisPermissionsCache(redis)
    } catch (err) {
        app.log.warn({ err: String(err) }, 'Failed to connect to Redis — running without cache')
        return new NoopPermissionsCache()
    }
}
