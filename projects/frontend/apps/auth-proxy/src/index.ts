import cookie from '@fastify/cookie'
import cors from '@fastify/cors'
import rateLimit from '@fastify/rate-limit'
import fastify, { type FastifyRequest } from 'fastify'
import { parseConfig, type Config } from './config'
import {
    createPermissionsCache,
    type PermissionsCache,
} from './permissions/cache'
import { permissionsPlugin } from './plugins/permissions'
import { requestContextPlugin } from './plugins/requestContext'
import { sessionSecurityPlugin } from './plugins/sessionSecurity'
import { proxyRoutesPlugin } from './proxy/register'
import { registerAuthRoutes } from './routes/auth'
import { getTraceContext } from './security'

export {
    InMemoryPermissionsCache,
    type PermissionsCache,
} from './permissions/cache'
export { parseConfig, type Config } from './config'
export { getOutgoingRequestHeaders, parseCookies } from './security'

export async function buildServer(config: Config, injectedCache?: PermissionsCache) {
    const app = fastify({
        logger: {
            level: config.logLevel,
            serializers: {
                req: (request) => {
                    const { traceId, requestId } = getTraceContext(
                        request as FastifyRequest
                    )
                    return {
                        method: request.method,
                        url: request.url,
                        trace_id: traceId,
                        request_id: requestId,
                    }
                },
                res: (response) => ({ statusCode: response.statusCode }),
            },
            redact: [
                'req.headers.authorization',
                'request.headers.authorization',
                'req.headers.cookie',
                'request.headers.cookie',
                'reply.headers.set-cookie',
                'response.headers.set-cookie',
            ],
        },
        trustProxy: true,
    })

    await app.register(cookie)
    await app.register(cors, {
        origin: config.corsOrigins,
        credentials: true,
        methods: ['GET', 'HEAD', 'POST', 'PUT', 'PATCH', 'DELETE', 'OPTIONS'],
        allowedHeaders: [
            'Accept',
            'Accept-Language',
            'Content-Language',
            'Content-Type',
            'Authorization',
            'X-Trace-Id',
            'X-Request-Id',
            'X-CSRF-Token',
            'X-Project-Id',
            'X-User-Id',
        ],
        exposedHeaders: ['X-Trace-Id', 'X-Request-Id'],
    })
    await app.register(rateLimit, {
        max: config.rateLimitMax,
        timeWindow: config.rateLimitWindowMs,
        allowList: (request) => request.url === '/health',
    })

    const permissionsCache = await createPermissionsCache(app, config, injectedCache)

    // These plugins intentionally break Fastify encapsulation so their hooks
    // apply to the auth and proxy route plugins registered below.
    await app.register(requestContextPlugin)
    await app.register(sessionSecurityPlugin, { config })
    await app.register(permissionsPlugin, { config, cache: permissionsCache })

    await app.register(registerAuthRoutes, { config })
    await app.register(proxyRoutesPlugin, { config })
    app.get('/health', async () => ({ status: 'ok' }))

    return app
}

async function start() {
    const config = parseConfig()
    const app = await buildServer(config)

    try {
        await app.listen({ port: config.port, host: '0.0.0.0' })
        app.log.info(
            { port: config.port, upstream: config.targetExperimentUrl },
            'Auth proxy started'
        )
    } catch (err) {
        app.log.error(err, 'Failed to start auth proxy')
        process.exit(1)
    }
}

if (require.main === module) {
    void start()
}
