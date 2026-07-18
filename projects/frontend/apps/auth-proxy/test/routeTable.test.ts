import { buildProxyRouteTable, ProxyRouteConfig } from '../src/proxy/routes'

const config: ProxyRouteConfig = {
    authUrl: 'http://auth-service:8001',
    targetExperimentUrl: 'http://experiment-service:8002',
    targetTelemetryUrl: 'http://telemetry-ingest-service:8003',
    targetScriptUrl: 'http://script-service:8004',
    targetConfigServiceUrl: 'http://config-service:8005',
}

describe('buildProxyRouteTable', () => {
    it('maps public prefixes to the expected upstream service', () => {
        const routesByPrefix = new Map(
            buildProxyRouteTable(config).map((route) => [route.prefix, route])
        )

        expect(routesByPrefix.get('/projects')?.upstream).toBe(config.authUrl)
        expect(routesByPrefix.get('/auth/admin')?.upstream).toBe(config.authUrl)
        expect(routesByPrefix.get('/api/v1/users')?.upstream).toBe(config.authUrl)
        expect(routesByPrefix.get('/api/v1/system-roles')?.upstream).toBe(config.authUrl)
        expect(routesByPrefix.get('/api/v1/permissions')?.upstream).toBe(config.authUrl)
        expect(routesByPrefix.get('/api/v1/audit-log')?.upstream).toBe(config.authUrl)
        expect(routesByPrefix.get('/api/v1/projects')?.upstream).toBe(config.authUrl)
        expect(routesByPrefix.get('/api/v1/telemetry')?.upstream).toBe(
            config.targetTelemetryUrl
        )
        expect(routesByPrefix.get('/api/v1/scripts')?.upstream).toBe(config.targetScriptUrl)
        expect(routesByPrefix.get('/api/v1/executions')?.upstream).toBe(
            config.targetScriptUrl
        )
        expect(routesByPrefix.get('/api/config-service')?.upstream).toBe(
            config.targetConfigServiceUrl
        )
        expect(routesByPrefix.get('/api')?.upstream).toBe(config.targetExperimentUrl)
    })

    it('keeps generic /api catch-all after more specific /api routes', () => {
        const routes = buildProxyRouteTable(config)
        const genericApiIndex = routes.findIndex((route) => route.prefix === '/api')

        expect(genericApiIndex).toBe(routes.length - 1)
        expect(routes[genericApiIndex].websocket).toBe(true)
    })
})
