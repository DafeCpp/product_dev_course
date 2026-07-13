import fp from 'fastify-plugin'
import { getTraceContext } from '../security'

export const requestContextPlugin = fp(async (app) => {
    app.addHook('onRequest', async (request, reply) => {
        const { traceId, requestId } = getTraceContext(request)
        request.traceId = traceId
        request.requestId = requestId
        request.log = request.log.child({
            trace_id: traceId,
            request_id: requestId,
            service: 'auth-proxy',
        })
        request.log.info({ method: request.method, url: request.url }, 'Incoming request')

        const accept = String(request.headers.accept || '')
        const isSse =
            accept.includes('text/event-stream') ||
            request.url.startsWith('/api/v1/telemetry/stream')
        if (!isSse) return

        try {
            request.raw.setTimeout?.(0)
            request.raw.socket?.setTimeout?.(0)
            reply.raw.setTimeout?.(0)
        } catch (err) {
            request.log.warn({ err }, 'SSE hardening failed')
        }
    })

    app.addHook('onResponse', async (request, reply) => {
        const logData: Record<string, unknown> = {
            method: request.method,
            url: request.url,
            statusCode: reply.statusCode,
        }
        if (reply.statusCode >= 400) logData.error = 'Request failed'
        request.log.info(logData, 'Request completed')
    })

    app.addHook('onSend', async (request, reply, payload) => {
        const contentType = String(reply.getHeader('content-type') || '')
        const isSse =
            contentType.includes('text/event-stream') ||
            String(request.headers.accept || '').includes('text/event-stream') ||
            request.url.startsWith('/api/v1/telemetry/stream')

        if (isSse && request.raw.socket && !reply.raw.headersSent) {
            reply.header('Cache-Control', 'no-cache, no-transform')
            reply.header('X-Accel-Buffering', 'no')
            if (!contentType) reply.header('Content-Type', 'text/event-stream; charset=utf-8')
        }

        if (request.url.startsWith('/projects') || request.url.startsWith('/api/')) {
            app.log.debug(
                {
                    method: request.method,
                    url: request.url,
                    status_code: reply.statusCode,
                    trace_id: request.traceId,
                },
                'Sending response to client'
            )
        }
        return payload
    })

    app.addHook('onError', async (request, _reply, error) => {
        request.log.error(
            {
                method: request.method,
                url: request.url,
                error: error.message,
                stack: error.stack,
            },
            'Proxy error'
        )
    })
}, { name: 'request-context' })
