import type {
  ConfigResponse,
  ConfigsListResponse,
  ConfigCreate,
  ConfigPatch,
  ConfigType,
  DryRunResponse,
  ConfigHistoryResponse,
  ConfigSchema,
  SchemasListResponse,
} from '../types/configs'
import { createAuthProxyClient } from './http/axiosInstance'

// config-service is reached through auth-proxy under the /api/config-service prefix
// (ADR-009). The proxy rewrites it to /api/v1/... on the upstream.
const BASE = '/api/config-service/v1'

const client = createAuthProxyClient()

export interface ConfigsListParams {
  service?: string
  project?: string
  config_type?: ConfigType
  is_active?: boolean
  limit?: number
  cursor?: string
}

function parseEtag(header: unknown): number {
  if (typeof header !== 'string') return NaN
  return parseInt(header.replace(/"/g, ''), 10)
}

export const configsApi = {
  listConfigs: async (params: ConfigsListParams = {}): Promise<ConfigsListResponse> => {
    const res = await client.get<ConfigsListResponse>(`${BASE}/config`, { params })
    return res.data
  },

  getConfig: async (id: string): Promise<{ config: ConfigResponse; etag: number }> => {
    const res = await client.get<ConfigResponse>(`${BASE}/config/${id}`)
    return { config: res.data, etag: parseEtag(res.headers['etag']) }
  },

  createConfig: async (
    data: ConfigCreate,
    opts: { idempotencyKey?: string } = {},
  ): Promise<{ config: ConfigResponse; etag: number }> => {
    const headers: Record<string, string> = {}
    if (opts.idempotencyKey) headers['Idempotency-Key'] = opts.idempotencyKey
    const res = await client.post<ConfigResponse>(`${BASE}/config`, data, { headers })
    return { config: res.data, etag: parseEtag(res.headers['etag']) }
  },

  dryRunCreate: async (data: ConfigCreate): Promise<DryRunResponse> => {
    const res = await client.post<DryRunResponse>(`${BASE}/config`, data, {
      params: { dry_run: true },
    })
    return res.data
  },

  patchConfig: async (
    id: string,
    data: ConfigPatch,
  ): Promise<{ config: ConfigResponse; etag: number }> => {
    const res = await client.patch<ConfigResponse>(`${BASE}/config/${id}`, data, {
      headers: { 'If-Match': `"${data.version}"` },
    })
    return { config: res.data, etag: parseEtag(res.headers['etag']) }
  },

  dryRunPatch: async (id: string, data: ConfigPatch): Promise<DryRunResponse> => {
    const res = await client.patch<DryRunResponse>(`${BASE}/config/${id}`, data, {
      params: { dry_run: true },
      headers: { 'If-Match': `"${data.version}"` },
    })
    return res.data
  },

  deleteConfig: async (
    id: string,
    opts: { version: number; changeReason: string },
  ): Promise<void> => {
    await client.delete(`${BASE}/config/${id}`, {
      params: { version: opts.version, change_reason: opts.changeReason },
      headers: { 'If-Match': `"${opts.version}"` },
    })
  },

  activate: async (
    id: string,
    opts: { version: number; changeReason?: string },
  ): Promise<ConfigResponse> => {
    const res = await client.post<ConfigResponse>(
      `${BASE}/config/${id}/activate`,
      { version: opts.version, change_reason: opts.changeReason },
      { headers: { 'If-Match': `"${opts.version}"` } },
    )
    return res.data
  },

  deactivate: async (
    id: string,
    opts: { version: number; changeReason?: string },
  ): Promise<ConfigResponse> => {
    const res = await client.post<ConfigResponse>(
      `${BASE}/config/${id}/deactivate`,
      { version: opts.version, change_reason: opts.changeReason },
      { headers: { 'If-Match': `"${opts.version}"` } },
    )
    return res.data
  },

  rollback: async (
    id: string,
    opts: { version: number; targetVersion: number; changeReason?: string },
  ): Promise<ConfigResponse> => {
    const res = await client.post<ConfigResponse>(
      `${BASE}/config/${id}/rollback`,
      {
        version: opts.version,
        target_version: opts.targetVersion,
        change_reason: opts.changeReason,
      },
      { headers: { 'If-Match': `"${opts.version}"` } },
    )
    return res.data
  },

  getHistory: async (id: string): Promise<ConfigHistoryResponse> => {
    const res = await client.get<ConfigHistoryResponse>(`${BASE}/config/${id}/history`)
    return res.data
  },

  listSchemas: async (): Promise<SchemasListResponse> => {
    const res = await client.get<SchemasListResponse>(`${BASE}/schemas`)
    return res.data
  },

  getSchema: async (configType: ConfigType): Promise<ConfigSchema> => {
    const res = await client.get<ConfigSchema>(`${BASE}/schemas/${configType}`)
    return res.data
  },

  updateSchema: async (
    configType: ConfigType,
    schema: Record<string, unknown>,
  ): Promise<ConfigSchema> => {
    const res = await client.put<ConfigSchema>(`${BASE}/schemas/${configType}`, { schema })
    return res.data
  },
}
