import { describe, it, expect, vi, beforeEach } from 'vitest'

const { http, apiGet, apiPost, apiPatch, apiDelete, apiClient } = vi.hoisted(() => ({
  http: { get: vi.fn(), post: vi.fn(), patch: vi.fn(), delete: vi.fn() },
  apiGet: vi.fn(), apiPost: vi.fn(), apiPatch: vi.fn(), apiDelete: vi.fn(),
  apiClient: { get: vi.fn() },
}))

vi.mock('./http/axiosInstance', () => ({ createAuthProxyClient: () => http }))
vi.mock('./client', () => ({ apiGet, apiPost, apiPatch, apiDelete, apiClient }))
vi.mock('../utils/activeProject', () => ({ getActiveProjectId: vi.fn(() => 'active-p') }))

import { authApi } from './auth'
import { sensorsApi, conversionProfilesApi, backfillApi } from './sensors'
import { experimentsApi } from './experiments'
import { runsApi } from './runs'

beforeEach(() => {
  vi.clearAllMocks()
  http.get.mockResolvedValue({ data: 'g' })
  http.post.mockResolvedValue({ data: 'p' })
  http.patch.mockResolvedValue({ data: 'u' })
  http.delete.mockResolvedValue({ data: undefined })
  apiGet.mockResolvedValue('G'); apiPost.mockResolvedValue('P'); apiPatch.mockResolvedValue('U'); apiDelete.mockResolvedValue(undefined)
  apiClient.get.mockResolvedValue({ data: 'csv' })
})

describe('authApi', () => {
  it('covers session endpoints', async () => {
    expect(await authApi.register({ username: 'u' } as never)).toBe('p')
    expect(http.post).toHaveBeenLastCalledWith('/auth/register', { username: 'u' })
    expect(await authApi.refresh()).toBe('p')
    expect(http.post).toHaveBeenLastCalledWith('/auth/refresh')
    await authApi.logout()
    expect(http.post).toHaveBeenLastCalledWith('/auth/logout')
    expect(await authApi.me()).toBe('g')
    expect(http.get).toHaveBeenLastCalledWith('/auth/me')
  })

  it('builds the admin user query string', async () => {
    await authApi.adminListUsers()
    expect(http.get).toHaveBeenLastCalledWith('/auth/admin/users')
    await authApi.adminListUsers({ search: 'bo', is_active: false })
    expect(http.get).toHaveBeenLastCalledWith('/auth/admin/users?search=bo&is_active=false')
    await authApi.adminListUsers({ search: '' })
    expect(http.get).toHaveBeenLastCalledWith('/auth/admin/users')
  })

  it('updates, deletes and resets users', async () => {
    expect(await authApi.adminUpdateUser('u1', { is_admin: true })).toBe('u')
    expect(http.patch).toHaveBeenLastCalledWith('/auth/admin/users/u1', { is_admin: true })
    await authApi.adminDeleteUser('u1')
    expect(http.delete).toHaveBeenLastCalledWith('/auth/admin/users/u1')
    await authApi.adminResetUserPassword('u1')
    expect(http.post).toHaveBeenLastCalledWith('/auth/admin/users/u1/reset', {})
    await authApi.adminResetUserPassword('u1', 'S3cret!')
    expect(http.post).toHaveBeenLastCalledWith('/auth/admin/users/u1/reset', { new_password: 'S3cret!' })
  })

  it('manages invites', async () => {
    await authApi.adminCreateInvite({})
    expect(http.post).toHaveBeenLastCalledWith('/auth/admin/invites', { expires_in_hours: 72 })
    await authApi.adminCreateInvite({ email_hint: 'a@b.c', expires_in_hours: 5 })
    expect(http.post).toHaveBeenLastCalledWith('/auth/admin/invites', { expires_in_hours: 5, email_hint: 'a@b.c' })
    await authApi.adminListInvites()
    expect(http.get).toHaveBeenLastCalledWith('/auth/admin/invites')
    await authApi.adminListInvites(true)
    expect(http.get).toHaveBeenLastCalledWith('/auth/admin/invites?active_only=true')
    await authApi.adminRevokeInvite('tok')
    expect(http.delete).toHaveBeenLastCalledWith('/auth/admin/invites/tok')
  })

  it('handles password flows', async () => {
    await authApi.changePassword({ old_password: 'a', new_password: 'b' })
    expect(http.post).toHaveBeenLastCalledWith('/auth/change-password', { old_password: 'a', new_password: 'b' })
    expect(await authApi.requestPasswordReset('a@b.c')).toBe('p')
    expect(http.post).toHaveBeenLastCalledWith('/auth/password-reset/request', { email: 'a@b.c' })
    await authApi.confirmPasswordReset('t', 'pw')
    expect(http.post).toHaveBeenLastCalledWith('/auth/password-reset/confirm', { reset_token: 't', new_password: 'pw' })
  })
})

describe('sensorsApi', () => {
  it('maps every method to its endpoint', async () => {
    await sensorsApi.list({ limit: 5 })
    expect(apiGet).toHaveBeenLastCalledWith('/api/v1/sensors', { params: { limit: 5 } })
    await sensorsApi.get('s', { project_id: 'p' })
    expect(apiGet).toHaveBeenLastCalledWith('/api/v1/sensors/s', { params: { project_id: 'p' } })
    await sensorsApi.create({ name: 'n' } as never)
    expect(apiPost).toHaveBeenLastCalledWith('/api/v1/sensors', { name: 'n' })
    await sensorsApi.update('s', { name: 'm' } as never, { project_id: 'p' })
    expect(apiPatch).toHaveBeenLastCalledWith('/api/v1/sensors/s', { name: 'm' }, { params: { project_id: 'p' } })
    await sensorsApi.delete('s')
    expect(apiDelete).toHaveBeenLastCalledWith('/api/v1/sensors/s', { params: undefined })
    await sensorsApi.rotateToken('s')
    expect(apiPost).toHaveBeenLastCalledWith('/api/v1/sensors/s/rotate-token', {}, { params: undefined })
    await sensorsApi.getStatusSummary('p')
    expect(apiGet).toHaveBeenLastCalledWith('/api/v1/sensors/status-summary', { params: { project_id: 'p' } })
    await sensorsApi.getHeartbeatHistory('s')
    expect(apiGet).toHaveBeenLastCalledWith('/api/v1/sensors/s/heartbeat-history', { params: { minutes: 60 } })
    await sensorsApi.getHeartbeatHistory('s', 5)
    expect(apiGet).toHaveBeenLastCalledWith('/api/v1/sensors/s/heartbeat-history', { params: { minutes: 5 } })
    await sensorsApi.getProjects('s')
    expect(apiGet).toHaveBeenLastCalledWith('/api/v1/sensors/s/projects')
    await sensorsApi.addProject('s', 'p')
    expect(apiPost).toHaveBeenLastCalledWith('/api/v1/sensors/s/projects', { project_id: 'p' })
    await sensorsApi.removeProject('s', 'p')
    expect(apiDelete).toHaveBeenLastCalledWith('/api/v1/sensors/s/projects/p', { params: { project_id: 'p' } })
    await sensorsApi.getErrorLog('s', { limit: 3 })
    expect(apiGet).toHaveBeenLastCalledWith('/api/v1/sensors/s/error-log', { params: { limit: 3 } })
  })

  it('covers conversion profiles and backfill', async () => {
    await conversionProfilesApi.list('s', { limit: 1 })
    expect(apiGet).toHaveBeenLastCalledWith('/api/v1/sensors/s/conversion-profiles', { params: { limit: 1 } })
    await conversionProfilesApi.create('s', { x: 1 } as never)
    expect(apiPost).toHaveBeenLastCalledWith('/api/v1/sensors/s/conversion-profiles', { x: 1 })
    await conversionProfilesApi.publish('s', 'c')
    expect(apiPost).toHaveBeenLastCalledWith('/api/v1/sensors/s/conversion-profiles/c/publish', {})
    await backfillApi.start('s')
    expect(apiPost).toHaveBeenLastCalledWith('/api/v1/sensors/s/backfill', {})
    await backfillApi.list('s')
    expect(apiGet).toHaveBeenLastCalledWith('/api/v1/sensors/s/backfill', { params: undefined })
    await backfillApi.get('s', 't')
    expect(apiGet).toHaveBeenLastCalledWith('/api/v1/sensors/s/backfill/t')
  })
})

describe('experimentsApi / runsApi', () => {
  it('archives and exports experiments', async () => {
    await experimentsApi.archive('e', { project_id: 'p' })
    expect(apiPost).toHaveBeenLastCalledWith('/api/v1/experiments/e/archive', {}, { params: { project_id: 'p' } })
    expect(await experimentsApi.exportData({ format: 'csv' })).toBe('csv')
    expect(apiClient.get).toHaveBeenLastCalledWith('/api/v1/experiments/export', {
      params: { format: 'csv', project_id: 'active-p' }, responseType: 'text',
    })
    await experimentsApi.exportData({ project_id: 'explicit' })
    expect(apiClient.get).toHaveBeenLastCalledWith('/api/v1/experiments/export', {
      params: { project_id: 'explicit' }, responseType: 'text',
    })
  })

  it('covers run helpers and export', async () => {
    await runsApi.complete('r')
    expect(apiPatch).toHaveBeenLastCalledWith('/api/v1/runs/r', { status: 'succeeded' })
    await runsApi.fail('r', 'why')
    expect(apiPatch).toHaveBeenLastCalledWith('/api/v1/runs/r', { status: 'failed', reason: 'why' })
    await runsApi.exportData('e', { format: 'json' })
    expect(apiClient.get).toHaveBeenLastCalledWith('/api/v1/experiments/e/runs/export', {
      params: { format: 'json', project_id: 'active-p' }, responseType: 'text',
    })
    await runsApi.bulkTags({ run_ids: ['r'], add_tags: ['t'] })
    expect(apiPost).toHaveBeenLastCalledWith('/api/v1/runs:bulk-tags', { run_ids: ['r'], add_tags: ['t'] })
  })
})
