import { afterEach, describe, expect, test, vi } from 'vitest'
import { InMemoryPermissionsCache } from '../src/index'
import { makeJwt, sessionHeaders, startProxy, type Proxy, type UpstreamHandler } from './helpers'

let proxy: Proxy | undefined

afterEach(async () => {
    await proxy?.dispose()
    proxy = undefined
})

const echoHeaders: UpstreamHandler = (request, reply) => reply.send({ headers: request.headers })
const PERMS = {
    user_id: 'u1',
    is_superadmin: false,
    system_permissions: ['experiments.read'],
    project_permissions: ['project.view'],
}
const user = (claims: Record<string, unknown> = {}) => makeJwt({ sub: 'u1', sys: ['sys.read'], ...claims })
const permsPath = 'GET /api/v1/users/u1/effective-permissions'

async function identityHeaders(p: Proxy, url: string, cookie: string) {
    const res = await p.app.inject({ method: 'GET', url, headers: { cookie } })
    expect(res.statusCode).toBe(200)
    return res.json().headers as Record<string, string | undefined>
}

describe('RBAC enrichment of /api requests', () => {
    test('anonymous requests carry no identity headers', async () => {
        proxy = await startProxy({ 'GET /api/v1/experiments': echoHeaders })

        const res = await proxy.app.inject({ method: 'GET', url: '/api/v1/experiments' })

        const headers = res.json().headers
        expect(headers['x-user-id']).toBeUndefined()
        expect(headers['x-user-is-superadmin']).toBeUndefined()
    })

    test('a token that is not a JWT yields no permissions headers', async () => {
        proxy = await startProxy({ 'GET /api/v1/experiments': echoHeaders })

        const headers = await identityHeaders(proxy, '/api/v1/experiments', 'access_token=opaque')

        expect(headers.authorization).toBe('Bearer opaque')
        expect(headers['x-user-is-superadmin']).toBeUndefined()
    })

    test('a token without a user id yields no identity', async () => {
        proxy = await startProxy({ 'GET /api/v1/experiments': echoHeaders })

        const headers = await identityHeaders(
            proxy,
            '/api/v1/experiments',
            `access_token=${makeJwt({ sys: ['x'] })}`
        )

        expect(headers['x-user-id']).toBeUndefined()
        expect(headers['x-user-is-superadmin']).toBeUndefined()
    })

    test('superadmins get blanket headers without an auth-service lookup', async () => {
        proxy = await startProxy({ 'GET /api/v1/experiments': echoHeaders })

        const headers = await identityHeaders(
            proxy,
            '/api/v1/experiments?project_id=p1',
            `access_token=${user({ sa: true })}`
        )

        expect(headers['x-user-is-superadmin']).toBe('true')
        expect(headers['x-user-system-permissions']).toBe('')
        expect(headers['x-project-id']).toBe('p1')
        expect(proxy.upstream.calls.some((call) => call.url.includes('effective-permissions'))).toBe(false)
    })

    test('without a project, only the system permissions from the token are used', async () => {
        proxy = await startProxy({ 'GET /api/v1/experiments': echoHeaders })

        const headers = await identityHeaders(proxy, '/api/v1/experiments', `access_token=${user()}`)

        expect(headers['x-user-id']).toBe('u1')
        expect(headers['x-user-is-superadmin']).toBe('false')
        expect(headers['x-user-system-permissions']).toBe('sys.read')
        expect(headers['x-user-permissions']).toBe('')
    })

    test('ignores non-string system permissions in the token', async () => {
        proxy = await startProxy({ 'GET /api/v1/experiments': echoHeaders })

        const headers = await identityHeaders(
            proxy,
            '/api/v1/experiments',
            `access_token=${user({ sys: ['a', 1, null, 'b'] })}`
        )

        expect(headers['x-user-system-permissions']).toBe('a,b')
    })

    test('a token without sys claims has no system permissions', async () => {
        proxy = await startProxy({ 'GET /api/v1/experiments': echoHeaders })

        const headers = await identityHeaders(
            proxy,
            '/api/v1/experiments',
            `access_token=${makeJwt({ user_id: 'u1' })}`
        )

        expect(headers['x-user-id']).toBe('u1')
        expect(headers['x-user-system-permissions']).toBe('')
    })

    test('fetches effective permissions for a project once and then serves them from cache', async () => {
        proxy = await startProxy(
            {
                'GET /api/v1/experiments': echoHeaders,
                [permsPath]: (_request, reply) => reply.send(PERMS),
            },
            {},
            new InMemoryPermissionsCache()
        )

        const first = await identityHeaders(proxy, '/api/v1/experiments?project_id=p1', `access_token=${user()}`)
        const second = await identityHeaders(proxy, '/api/v1/experiments?project_id=p1', `access_token=${user()}`)

        for (const headers of [first, second]) {
            expect(headers['x-user-system-permissions']).toBe('experiments.read')
            expect(headers['x-user-permissions']).toBe('project.view')
            expect(headers['x-project-id']).toBe('p1')
        }
        const lookups = proxy.upstream.calls.filter((call) => call.url.includes('effective-permissions'))
        expect(lookups).toHaveLength(1)
        expect(lookups[0].url).toBe('/api/v1/users/u1/effective-permissions?project_id=p1')
    })

    test('takes the first project_id when the query repeats it', async () => {
        proxy = await startProxy({
            'GET /api/v1/experiments': echoHeaders,
            [permsPath]: (_request, reply) => reply.send(PERMS),
        })

        const headers = await identityHeaders(
            proxy,
            '/api/v1/experiments?project_id=p1&project_id=p2',
            `access_token=${user()}`
        )

        expect(headers['x-project-id']).toBe('p1')
    })

    test('falls back to token permissions when auth-service refuses the lookup', async () => {
        proxy = await startProxy({
            'GET /api/v1/experiments': echoHeaders,
            [permsPath]: (_request, reply) => reply.status(500).send({}),
        })

        const headers = await identityHeaders(proxy, '/api/v1/experiments?project_id=p1', `access_token=${user()}`)

        expect(headers['x-user-system-permissions']).toBe('sys.read')
        expect(headers['x-user-permissions']).toBe('')
    })

    test('falls back to token permissions when auth-service is unreachable', async () => {
        proxy = await startProxy(
            { 'GET /api/v1/experiments': echoHeaders },
            { authUrl: 'http://127.0.0.1:1' }
        )

        const headers = await identityHeaders(proxy, '/api/v1/experiments?project_id=p1', `access_token=${user()}`)

        expect(headers['x-user-system-permissions']).toBe('sys.read')
        expect(headers['x-user-permissions']).toBe('')
    })

    test('reads project_id from a JSON body', async () => {
        proxy = await startProxy({
            'POST /api/v1/experiments': echoHeaders,
            [permsPath]: (_request, reply) => reply.send(PERMS),
        })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/api/v1/experiments',
            headers: sessionHeaders(`access_token=${user()}`),
            payload: { project_id: 'p-body', name: 'x' },
        })

        const headers = res.json().headers
        expect(headers['x-project-id']).toBe('p-body')
        expect(headers['x-user-permissions']).toBe('project.view')
        expect(proxy.upstream.calls.some((call) => call.url.endsWith('project_id=p-body'))).toBe(true)
    })

    test.each([
        ['a non-string project_id', { project_id: 42 }],
        ['a JSON array', [{ project_id: 'p1' }]],
        ['no project_id', { name: 'x' }],
    ])('ignores %s in the body', async (_name, payload) => {
        proxy = await startProxy({ 'POST /api/v1/experiments': echoHeaders })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/api/v1/experiments',
            headers: sessionHeaders(`access_token=${user()}`),
            payload: JSON.stringify(payload),
        })

        expect(res.json().headers['x-project-id']).toBeUndefined()
    })

    test('does not touch non-JSON request bodies', async () => {
        proxy = await startProxy({ 'POST /api/v1/experiments': echoHeaders })

        const res = await proxy.app.inject({
            method: 'POST',
            url: '/api/v1/experiments',
            headers: sessionHeaders(`access_token=${user()}`, { 'content-type': 'text/plain' }),
            payload: 'project_id=p1',
        })

        expect(res.statusCode).toBe(200)
        expect(res.json().headers['x-project-id']).toBeUndefined()
    })
})

describe('X-Project-Ids for the all-sensors listing', () => {
    const sensors = 'GET /api/v1/sensors'

    test('lists the projects of the user', async () => {
        proxy = await startProxy({
            [sensors]: echoHeaders,
            'GET /projects': (_request, reply) => reply.send({ projects: [{ id: 'p1' }, { id: 'p2' }] }),
        })

        const headers = await identityHeaders(proxy, '/api/v1/sensors', `access_token=${user()}`)

        expect(headers['x-project-ids']).toBe('p1,p2')
    })

    test('treats a response without projects as empty', async () => {
        proxy = await startProxy({
            [sensors]: echoHeaders,
            'GET /projects': (_request, reply) => reply.send({}),
        })

        const headers = await identityHeaders(proxy, '/api/v1/sensors', `access_token=${user()}`)

        expect(headers['x-project-ids']).toBeUndefined()
    })

    test('omits the header when the projects lookup fails', async () => {
        proxy = await startProxy({
            [sensors]: echoHeaders,
            'GET /projects': (_request, reply) => reply.status(500).send({}),
        })

        const headers = await identityHeaders(proxy, '/api/v1/sensors', `access_token=${user()}`)

        expect(headers['x-project-ids']).toBeUndefined()
    })

    test('omits the header when auth-service is unreachable', async () => {
        proxy = await startProxy({ [sensors]: echoHeaders }, { authUrl: 'http://127.0.0.1:1' })

        const headers = await identityHeaders(proxy, '/api/v1/sensors', `access_token=${user()}`)

        expect(headers['x-project-ids']).toBeUndefined()
    })

    test('is skipped for anonymous requests and when a project is selected', async () => {
        proxy = await startProxy({ [sensors]: echoHeaders })

        await proxy.app.inject({ method: 'GET', url: '/api/v1/sensors' })
        await identityHeaders(proxy, '/api/v1/sensors?project_id=p1', `access_token=${user()}`)

        expect(proxy.upstream.calls.some((call) => call.url.startsWith('/projects'))).toBe(false)
    })
})

describe('cache invalidation on authorization failures', () => {
    test.each([401, 403])('drops cached permissions after a %s from an /api upstream', async (status) => {
        const cache = new InMemoryPermissionsCache()
        const invalidate = vi.spyOn(cache, 'invalidateUser')
        proxy = await startProxy(
            { 'GET /api/v1/experiments': (_request, reply) => reply.status(status).send({}) },
            {},
            cache
        )

        const res = await proxy.app.inject({
            method: 'GET',
            url: '/api/v1/experiments',
            headers: { cookie: `access_token=${user()}` },
        })

        expect(res.statusCode).toBe(status)
        expect(invalidate).toHaveBeenCalledWith('u1')
    })

    test('does nothing for anonymous failures or when invalidation itself fails', async () => {
        const cache = new InMemoryPermissionsCache()
        const invalidate = vi.spyOn(cache, 'invalidateUser').mockRejectedValue(new Error('redis down'))
        proxy = await startProxy(
            { 'GET /api/v1/experiments': (_request, reply) => reply.status(401).send({}) },
            {},
            cache
        )

        const anonymous = await proxy.app.inject({ method: 'GET', url: '/api/v1/experiments' })
        const identified = await proxy.app.inject({
            method: 'GET',
            url: '/api/v1/experiments',
            headers: { cookie: `access_token=${user()}` },
        })

        expect(anonymous.statusCode).toBe(401)
        expect(identified.statusCode).toBe(401)
        expect(invalidate).toHaveBeenCalledTimes(1)
    })
})
