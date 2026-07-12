import { describe, it, expect, vi, beforeEach } from 'vitest'

const { mockAxiosInstance, mockCreate } = vi.hoisted(() => {
  const instance = {
    get: vi.fn(),
    post: vi.fn(),
    patch: vi.fn(),
    put: vi.fn(),
    delete: vi.fn(),
    interceptors: {
      request: { use: vi.fn() },
      response: { use: vi.fn() },
    },
  }
  const create = vi.fn(() => instance)
  return { mockAxiosInstance: instance, mockCreate: create }
})

vi.mock('axios', () => ({
  default: { create: mockCreate, post: vi.fn() },
}))

import { configsApi } from './configs'

const BASE = '/api/config-service/v1'

describe('configsApi', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('listConfigs GETs /config with filter params', async () => {
    mockAxiosInstance.get.mockResolvedValueOnce({ data: { items: [], next_cursor: null } })

    await configsApi.listConfigs({ service: 'auth-service', config_type: 'feature_flag', is_active: true })

    expect(mockAxiosInstance.get).toHaveBeenCalledWith(`${BASE}/config`, {
      params: { service: 'auth-service', config_type: 'feature_flag', is_active: true },
    })
  })

  it('getConfig parses the ETag header into a numeric version', async () => {
    mockAxiosInstance.get.mockResolvedValueOnce({
      data: { id: 'c1', version: 3 },
      headers: { etag: '"3"' },
    })

    const result = await configsApi.getConfig('c1')

    expect(mockAxiosInstance.get).toHaveBeenCalledWith(`${BASE}/config/c1`)
    expect(result.etag).toBe(3)
    expect(result.config).toMatchObject({ id: 'c1' })
  })

  it('createConfig POSTs and forwards Idempotency-Key', async () => {
    mockAxiosInstance.post.mockResolvedValueOnce({ data: { id: 'c1' }, headers: { etag: '"1"' } })

    const payload = { service_name: 'svc', key: 'k', config_type: 'feature_flag' as const, value: { enabled: true } }
    const result = await configsApi.createConfig(payload, { idempotencyKey: 'idem-1' })

    expect(mockAxiosInstance.post).toHaveBeenCalledWith(`${BASE}/config`, payload, {
      headers: { 'Idempotency-Key': 'idem-1' },
    })
    expect(result.etag).toBe(1)
  })

  it('dryRunCreate POSTs with dry_run=true', async () => {
    mockAxiosInstance.post.mockResolvedValueOnce({ data: { preview: {}, dry_run: true } })

    const payload = { service_name: 'svc', key: 'k', config_type: 'qos' as const, value: {} }
    await configsApi.dryRunCreate(payload)

    expect(mockAxiosInstance.post).toHaveBeenCalledWith(`${BASE}/config`, payload, {
      params: { dry_run: true },
    })
  })

  it('patchConfig sends If-Match from the version', async () => {
    mockAxiosInstance.patch.mockResolvedValueOnce({ data: { id: 'c1', version: 2 }, headers: { etag: '"2"' } })

    await configsApi.patchConfig('c1', { version: 1, value: { enabled: false } })

    expect(mockAxiosInstance.patch).toHaveBeenCalledWith(
      `${BASE}/config/c1`,
      { version: 1, value: { enabled: false } },
      { headers: { 'If-Match': '"1"' } },
    )
  })

  it('deleteConfig sends version + change_reason as query and If-Match header', async () => {
    mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

    await configsApi.deleteConfig('c1', { version: 4, changeReason: 'cleanup' })

    expect(mockAxiosInstance.delete).toHaveBeenCalledWith(`${BASE}/config/c1`, {
      params: { version: 4, change_reason: 'cleanup' },
      headers: { 'If-Match': '"4"' },
    })
  })

  it('activate POSTs to /activate with If-Match', async () => {
    mockAxiosInstance.post.mockResolvedValueOnce({ data: { id: 'c1', version: 2, is_active: true } })

    await configsApi.activate('c1', { version: 1, changeReason: 'go live' })

    expect(mockAxiosInstance.post).toHaveBeenCalledWith(
      `${BASE}/config/c1/activate`,
      { version: 1, change_reason: 'go live' },
      { headers: { 'If-Match': '"1"' } },
    )
  })

  it('rollback POSTs target_version', async () => {
    mockAxiosInstance.post.mockResolvedValueOnce({ data: { id: 'c1', version: 5 } })

    await configsApi.rollback('c1', { version: 4, targetVersion: 1, changeReason: 'revert' })

    expect(mockAxiosInstance.post).toHaveBeenCalledWith(
      `${BASE}/config/c1/rollback`,
      { version: 4, target_version: 1, change_reason: 'revert' },
      { headers: { 'If-Match': '"4"' } },
    )
  })

  it('getHistory GETs /config/{id}/history', async () => {
    mockAxiosInstance.get.mockResolvedValueOnce({ data: { items: [] } })

    await configsApi.getHistory('c1')

    expect(mockAxiosInstance.get).toHaveBeenCalledWith(`${BASE}/config/c1/history`)
  })

  it('listSchemas GETs /schemas and updateSchema PUTs schema', async () => {
    mockAxiosInstance.get.mockResolvedValueOnce({ data: { items: [] } })
    mockAxiosInstance.put.mockResolvedValueOnce({ data: { config_type: 'feature_flag' } })

    await configsApi.listSchemas()
    await configsApi.updateSchema('feature_flag', { type: 'object' })

    expect(mockAxiosInstance.get).toHaveBeenCalledWith(`${BASE}/schemas`)
    expect(mockAxiosInstance.put).toHaveBeenCalledWith(`${BASE}/schemas/feature_flag`, {
      schema: { type: 'object' },
    })
  })
})
