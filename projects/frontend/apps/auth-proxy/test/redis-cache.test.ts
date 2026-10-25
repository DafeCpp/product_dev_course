import { EventEmitter } from 'node:events'
import fastify from 'fastify'
import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest'
import { createPermissionsCache, type EffectivePermissions } from '../src/permissions/cache'
import { makeConfig } from './helpers'

const redisState = vi.hoisted(() => ({
    instances: [] as Array<Record<string, any>>,
    connectError: undefined as Error | undefined,
    quitError: undefined as Error | undefined,
    commandError: undefined as Error | undefined,
    scanBatches: [] as string[][],
    scanError: undefined as Error | undefined,
    store: new Map<string, string>(),
}))

vi.mock('ioredis', () => ({
    default: class FakeRedis extends EventEmitter {
        calls: Array<[string, ...unknown[]]> = []
        constructor(public url: string, public options: unknown) {
            super()
            redisState.instances.push(this as unknown as Record<string, any>)
        }
        async connect() {
            if (redisState.connectError) throw redisState.connectError
        }
        async quit() {
            this.calls.push(['quit'])
            if (redisState.quitError) throw redisState.quitError
        }
        async get(key: string) {
            if (redisState.commandError) throw redisState.commandError
            return redisState.store.get(key) ?? null
        }
        async setex(key: string, ttl: number, value: string) {
            if (redisState.commandError) throw redisState.commandError
            this.calls.push(['setex', key, ttl])
            redisState.store.set(key, value)
        }
        async del(...keys: string[]) {
            if (redisState.commandError) throw redisState.commandError
            this.calls.push(['del', ...keys])
        }
        scanStream(options: unknown) {
            this.calls.push(['scanStream', options])
            const stream = new EventEmitter()
            setImmediate(() => {
                if (redisState.scanError) {
                    stream.emit('error', redisState.scanError)
                    return
                }
                for (const batch of redisState.scanBatches) stream.emit('data', batch)
                stream.emit('end')
            })
            return stream
        }
    },
}))

const PERMS: EffectivePermissions = {
    user_id: 'u1',
    is_superadmin: false,
    system_permissions: ['read'],
    project_permissions: ['project:read'],
}

beforeEach(() => {
    redisState.instances.length = 0
    redisState.connectError = undefined
    redisState.quitError = undefined
    redisState.commandError = undefined
    redisState.scanBatches = []
    redisState.scanError = undefined
    redisState.store.clear()
})

describe('createPermissionsCache', () => {
    let app: ReturnType<typeof fastify>

    beforeEach(() => {
        app = fastify({ logger: false })
    })

    afterEach(async () => {
        await app.close()
    })

    test('prefers an injected cache', async () => {
        const injected = { get: vi.fn(), set: vi.fn(), invalidateUser: vi.fn() }

        const cache = await createPermissionsCache(app, makeConfig('http://auth', { redisUrl: 'redis://x' }), injected)

        expect(cache).toBe(injected)
        expect(redisState.instances).toHaveLength(0)
    })

    test('without Redis it falls back to a no-op cache', async () => {
        const cache = await createPermissionsCache(app, makeConfig('http://auth'))

        await cache.set('perms:sys:u1', PERMS, 30)
        expect(await cache.get('perms:sys:u1')).toBeNull()
        await expect(cache.invalidateUser('u1')).resolves.toBeUndefined()
    })

    test('a failed Redis connection degrades to the no-op cache', async () => {
        redisState.connectError = new Error('ECONNREFUSED')

        const cache = await createPermissionsCache(app, makeConfig('http://auth', { redisUrl: 'redis://x' }))

        await cache.set('perms:sys:u1', PERMS, 30)
        expect(await cache.get('perms:sys:u1')).toBeNull()
        expect(redisState.store.size).toBe(0)
    })

    test('stores and reads permissions through Redis with a TTL', async () => {
        const cache = await createPermissionsCache(app, makeConfig('http://auth', { redisUrl: 'redis://x' }))

        expect(await cache.get('perms:u1:p1')).toBeNull()
        await cache.set('perms:u1:p1', PERMS, 30)

        expect(await cache.get('perms:u1:p1')).toEqual(PERMS)
        expect(redisState.instances[0].calls).toContainEqual(['setex', 'perms:u1:p1', 30])
    })

    test('Redis command failures never break authorization', async () => {
        const cache = await createPermissionsCache(app, makeConfig('http://auth', { redisUrl: 'redis://x' }))
        redisState.commandError = new Error('READONLY')

        await expect(cache.set('perms:u1:p1', PERMS, 30)).resolves.toBeUndefined()
        expect(await cache.get('perms:u1:p1')).toBeNull()
        redisState.scanBatches = [['perms:u1:p1']]
        await expect(cache.invalidateUser('u1')).resolves.toBeUndefined()
    })

    test('invalidateUser deletes the system key and every scanned project key', async () => {
        const cache = await createPermissionsCache(app, makeConfig('http://auth', { redisUrl: 'redis://x' }))
        redisState.scanBatches = [['perms:u1:p1'], ['perms:u1:p2', 'perms:u1:p3']]

        await cache.invalidateUser('u1')

        const calls = redisState.instances[0].calls
        expect(calls).toContainEqual(['scanStream', { match: 'perms:u1:*', count: 100 }])
        expect(calls).toContainEqual([
            'del',
            'perms:sys:u1',
            'perms:u1:p1',
            'perms:u1:p2',
            'perms:u1:p3',
        ])
    })

    test('a scan error is swallowed and nothing is deleted', async () => {
        const cache = await createPermissionsCache(app, makeConfig('http://auth', { redisUrl: 'redis://x' }))
        redisState.scanError = new Error('scan failed')

        await expect(cache.invalidateUser('u1')).resolves.toBeUndefined()

        expect(redisState.instances[0].calls.some(([name]: [string]) => name === 'del')).toBe(false)
    })

    test('logs connection errors and quits Redis when the app closes', async () => {
        await createPermissionsCache(app, makeConfig('http://auth', { redisUrl: 'redis://x' }))
        const redis = redisState.instances[0]

        redis.emit('error', new Error('connection reset'))
        await app.close()

        expect(redis.calls).toContainEqual(['quit'])
    })

    test('a failing quit does not break shutdown', async () => {
        redisState.quitError = new Error('already closed')
        await createPermissionsCache(app, makeConfig('http://auth', { redisUrl: 'redis://x' }))

        await expect(app.close()).resolves.toBeUndefined()
    })
})
