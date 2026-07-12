export interface ProxyRouteConfig {
    authUrl: string
    targetExperimentUrl: string
    targetTelemetryUrl: string
    targetConfigServiceUrl: string
    targetScriptUrl: string
}

export type ProxyRouteKind =
    | 'auth-web'
    | 'auth-api'
    | 'telemetry'
    | 'script'
    | 'config-service'
    | 'experiment-service'

export type ProxyRouteName =
    | 'projects-web'
    | 'telemetry-ingest'
    | 'scripts-api'
    | 'executions-api'
    | 'users-api'
    | 'system-roles-api'
    | 'permissions-api'
    | 'audit-log-api'
    | 'projects-api'
    | 'config-service-api'
    | 'experiment-service-api'

export interface ProxyRouteDefinition {
    name: ProxyRouteName
    kind: ProxyRouteKind
    prefix: string
    upstream: string
    rewritePrefix: string
    websocket?: boolean
    deleteCookie?: boolean
    ensureJsonContentType?: boolean
}

export function buildProxyRouteTable(config: ProxyRouteConfig): ProxyRouteDefinition[] {
    return [
        {
            name: 'projects-web',
            kind: 'auth-web',
            prefix: '/projects',
            upstream: config.authUrl,
            rewritePrefix: '/projects',
            deleteCookie: false,
        },
        {
            name: 'telemetry-ingest',
            kind: 'telemetry',
            prefix: '/api/v1/telemetry',
            upstream: config.targetTelemetryUrl,
            rewritePrefix: '/api/v1/telemetry',
        },
        {
            name: 'scripts-api',
            kind: 'script',
            prefix: '/api/v1/scripts',
            upstream: config.targetScriptUrl,
            rewritePrefix: '/api/v1/scripts',
        },
        {
            name: 'executions-api',
            kind: 'script',
            prefix: '/api/v1/executions',
            upstream: config.targetScriptUrl,
            rewritePrefix: '/api/v1/executions',
        },
        {
            name: 'users-api',
            kind: 'auth-api',
            prefix: '/api/v1/users',
            upstream: config.authUrl,
            rewritePrefix: '/api/v1/users',
            deleteCookie: false,
            ensureJsonContentType: false,
        },
        {
            name: 'system-roles-api',
            kind: 'auth-api',
            prefix: '/api/v1/system-roles',
            upstream: config.authUrl,
            rewritePrefix: '/api/v1/system-roles',
            ensureJsonContentType: false,
        },
        {
            name: 'permissions-api',
            kind: 'auth-api',
            prefix: '/api/v1/permissions',
            upstream: config.authUrl,
            rewritePrefix: '/api/v1/permissions',
            ensureJsonContentType: false,
        },
        {
            name: 'audit-log-api',
            kind: 'auth-api',
            prefix: '/api/v1/audit-log',
            upstream: config.authUrl,
            rewritePrefix: '/api/v1/audit-log',
            ensureJsonContentType: false,
        },
        {
            name: 'projects-api',
            kind: 'auth-api',
            prefix: '/api/v1/projects',
            upstream: config.authUrl,
            rewritePrefix: '/api/v1/projects',
            ensureJsonContentType: false,
        },
        {
            name: 'config-service-api',
            kind: 'config-service',
            prefix: '/api/config-service',
            upstream: config.targetConfigServiceUrl,
            rewritePrefix: '/api',
        },
        {
            name: 'experiment-service-api',
            kind: 'experiment-service',
            prefix: '/api',
            upstream: config.targetExperimentUrl,
            rewritePrefix: '/api',
            websocket: true,
        },
    ]
}
