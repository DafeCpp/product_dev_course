import axios, { AxiosError, AxiosHeaders, type AxiosAdapter, type AxiosRequestConfig } from 'axios'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { HttpError } from '@lostpointer/web-http'
import { projectsClient } from './projectsClient'

vi.mock('../../utils/httpDebug', () => ({ maybeEmitHttpErrorToastFromAxiosError: vi.fn() }))
import { maybeEmitHttpErrorToastFromAxiosError } from '../../utils/httpDebug'

const originalAdapter = projectsClient.defaults.adapter
const originalLocation = window.location
function adapter(status: number, data: unknown): AxiosAdapter {
  return async (config) => {
    const response = { config, status, data, statusText: '', headers: new AxiosHeaders({ 'content-type': 'application/json' }) }
    if (status >= 400) throw new AxiosError('HTTP error', 'ERR_BAD_RESPONSE', config, undefined, response)
    return response
  }
}
afterEach(() => {
  projectsClient.defaults.adapter = originalAdapter
  Object.defineProperty(window, 'location', { value: originalLocation, writable: true })
  document.cookie = 'csrf_token=; Max-Age=0; path=/'
  vi.restoreAllMocks()
  vi.clearAllMocks()
})

describe('Projects shared transport pilot', () => {
  it('keeps credentials, trace, CSRF, timeout and project parameters', async () => {
    document.cookie = 'csrf_token=token; path=/'
    projectsClient.defaults.adapter = adapter(200, '{}')
    const response = await projectsClient.post('/api/v1/projects', { name: 'P' }, { params: { project_id: 'p' } })
    expect(response.config).toMatchObject({ withCredentials: true, timeout: 30000, params: { project_id: 'p' } })
    expect(response.config.headers.get('X-CSRF-Token')).toBe('token')
    expect(response.config.headers.get('X-Trace-Id')).toBeTruthy()
    expect(response.config.headers.get('X-Request-Id')).toBeTruthy()
  })
  it('refreshes a 401 and retries successfully before normalizing', async () => {
    let count = 0
    projectsClient.defaults.adapter = async (config) => adapter(++count === 1 ? 401 : 200, '{"id":"p"}')(config)
    const refresh = vi.spyOn(axios, 'post').mockResolvedValue({ data: {} })
    expect((await projectsClient.get('/api/v1/projects/p')).data).toEqual({ id: 'p' })
    expect(refresh).toHaveBeenCalledTimes(1)
    expect(refresh).toHaveBeenCalledWith(expect.stringMatching(/\/auth\/refresh$/), {}, { withCredentials: true })
    expect(count).toBe(2)
    expect(maybeEmitHttpErrorToastFromAxiosError).not.toHaveBeenCalled()
  })
  it('does not refresh sensor-token requests', async () => {
    projectsClient.defaults.adapter = adapter(401, '{"error":"invalid sensor token"}')
    const refresh = vi.spyOn(axios, 'post')
    const config: AxiosRequestConfig & { _skipAuthInterceptor: boolean } = { _skipAuthInterceptor: true }
    await expect(projectsClient.get('/api/test', config)).rejects.toMatchObject({ kind: 'api', status: 401, message: 'invalid sensor token' })
    expect(refresh).not.toHaveBeenCalled()
  })
  it('redirects on refresh failure and returns a normalized error', async () => {
    Object.defineProperty(window, 'location', { value: { href: '' }, writable: true })
    projectsClient.defaults.adapter = adapter(401, '{}')
    vi.spyOn(axios, 'post').mockRejectedValue(new AxiosError('offline', 'ERR_NETWORK'))
    await expect(projectsClient.get('/api/v1/projects')).rejects.toMatchObject({ kind: 'network' })
    expect(window.location.href).toBe('/login')
    expect(maybeEmitHttpErrorToastFromAxiosError).toHaveBeenCalledTimes(1)
  })
  it('does not retry a repeated 401 indefinitely', async () => {
    Object.defineProperty(window, 'location', { value: { href: '' }, writable: true })
    const request = vi.fn(adapter(401, '{}'))
    projectsClient.defaults.adapter = request
    const refresh = vi.spyOn(axios, 'post').mockResolvedValue({ data: {} })
    await expect(projectsClient.get('/api/v1/projects')).rejects.toBeInstanceOf(HttpError)
    expect(request).toHaveBeenCalledTimes(2)
    expect(refresh).toHaveBeenCalledTimes(1)
  })
  it.each(['ETIMEDOUT', 'ERR_NETWORK'])('normalizes %s without refresh', async (code) => {
    projectsClient.defaults.adapter = async () => { throw new AxiosError('failed', code) }
    const refresh = vi.spyOn(axios, 'post')
    await expect(projectsClient.get('/api/v1/projects')).rejects.toMatchObject({ kind: code === 'ETIMEDOUT' ? 'timeout' : 'network' })
    expect(refresh).not.toHaveBeenCalled()
  })
  it('honors cancellation', async () => {
    const controller = new AbortController()
    controller.abort()
    await expect(projectsClient.get('/api/v1/projects', { signal: controller.signal })).rejects.toMatchObject({ kind: 'aborted' })
  })
})
