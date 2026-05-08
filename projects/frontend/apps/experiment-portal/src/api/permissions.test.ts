import { describe, it, expect, vi, beforeEach } from 'vitest'

vi.mock('./client', () => ({
    apiGet: vi.fn(),
    apiPost: vi.fn(),
    apiPatch: vi.fn(),
    apiDelete: vi.fn(),
}))

import { permissionsApi } from './permissions'
import { apiGet, apiPost, apiPatch, apiDelete } from './client'

const mockApiGet = vi.mocked(apiGet)
const mockApiPost = vi.mocked(apiPost)
const mockApiPatch = vi.mocked(apiPatch)
const mockApiDelete = vi.mocked(apiDelete)

describe('permissionsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    describe('listPermissions', () => {
        it('calls GET /api/v1/permissions', async () => {
            const mockPerms = [{ id: 'p1', name: 'experiments.read' }]
            mockApiGet.mockResolvedValueOnce(mockPerms)
            const result = await permissionsApi.listPermissions()
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/permissions')
            expect(result).toEqual(mockPerms)
        })
    })

    describe('getEffectivePermissions', () => {
        it('calls GET /api/v1/users/{id}/effective-permissions with project_id', async () => {
            mockApiGet.mockResolvedValueOnce({ permissions: [] })
            await permissionsApi.getEffectivePermissions('user-1', 'proj-1')
            expect(mockApiGet).toHaveBeenCalledWith(
                '/api/v1/users/user-1/effective-permissions',
                { params: { project_id: 'proj-1' } },
            )
        })

        it('omits project_id when not provided', async () => {
            mockApiGet.mockResolvedValueOnce({ permissions: [] })
            await permissionsApi.getEffectivePermissions('user-1')
            expect(mockApiGet).toHaveBeenCalledWith(
                '/api/v1/users/user-1/effective-permissions',
                { params: {} },
            )
        })
    })

    describe('system roles', () => {
        it('listSystemRoles calls GET /api/v1/system-roles', async () => {
            mockApiGet.mockResolvedValueOnce([])
            await permissionsApi.listSystemRoles()
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/system-roles')
        })

        it('createSystemRole calls POST /api/v1/system-roles', async () => {
            const data = { name: 'admin', description: 'Admins', permissions: ['p1', 'p2'] }
            mockApiPost.mockResolvedValueOnce({ id: 'r1', ...data })
            await permissionsApi.createSystemRole(data)
            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/system-roles', data)
        })

        it('updateSystemRole calls PATCH /api/v1/system-roles/{id}', async () => {
            const data = { name: 'admin-renamed' }
            mockApiPatch.mockResolvedValueOnce({ id: 'r1', ...data })
            await permissionsApi.updateSystemRole('r1', data)
            expect(mockApiPatch).toHaveBeenCalledWith('/api/v1/system-roles/r1', data)
        })

        it('deleteSystemRole calls DELETE /api/v1/system-roles/{id}', async () => {
            mockApiDelete.mockResolvedValueOnce(undefined as any)
            await permissionsApi.deleteSystemRole('r1')
            expect(mockApiDelete).toHaveBeenCalledWith('/api/v1/system-roles/r1')
        })
    })

    describe('system role assignments', () => {
        it('grantSystemRole accepts string roleId with optional expiresAt', async () => {
            mockApiPost.mockResolvedValueOnce({ user_id: 'u1', role_id: 'r1' })
            await permissionsApi.grantSystemRole('u1', 'r1', '2026-01-01')
            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/users/u1/system-roles', {
                role_id: 'r1',
                expires_at: '2026-01-01',
            })
        })

        it('grantSystemRole without expiresAt omits the field', async () => {
            mockApiPost.mockResolvedValueOnce({ user_id: 'u1', role_id: 'r1' })
            await permissionsApi.grantSystemRole('u1', 'r1')
            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/users/u1/system-roles', {
                role_id: 'r1',
            })
        })

        it('grantSystemRole accepts body object', async () => {
            mockApiPost.mockResolvedValueOnce({ user_id: 'u1', role_id: 'r1' })
            await permissionsApi.grantSystemRole('u1', { role_id: 'r1', expires_at: '2026-01-01' })
            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/users/u1/system-roles', {
                role_id: 'r1',
                expires_at: '2026-01-01',
            })
        })

        it('revokeSystemRole calls DELETE /api/v1/users/{user}/system-roles/{role}', async () => {
            mockApiDelete.mockResolvedValueOnce(undefined as any)
            await permissionsApi.revokeSystemRole('u1', 'r1')
            expect(mockApiDelete).toHaveBeenCalledWith('/api/v1/users/u1/system-roles/r1')
        })
    })

    describe('project roles', () => {
        it('listProjectRoles calls GET /api/v1/projects/{id}/roles', async () => {
            mockApiGet.mockResolvedValueOnce([])
            await permissionsApi.listProjectRoles('p1')
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/projects/p1/roles')
        })

        it('createProjectRole calls POST /api/v1/projects/{id}/roles', async () => {
            const data = { name: 'editor', permissions: ['p1'] }
            mockApiPost.mockResolvedValueOnce({ id: 'r1', ...data })
            await permissionsApi.createProjectRole('p1', data)
            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/projects/p1/roles', data)
        })

        it('updateProjectRole calls PATCH /api/v1/projects/{id}/roles/{role}', async () => {
            mockApiPatch.mockResolvedValueOnce({ id: 'r1' })
            await permissionsApi.updateProjectRole('p1', 'r1', { name: 'viewer' })
            expect(mockApiPatch).toHaveBeenCalledWith('/api/v1/projects/p1/roles/r1', { name: 'viewer' })
        })

        it('deleteProjectRole calls DELETE /api/v1/projects/{id}/roles/{role}', async () => {
            mockApiDelete.mockResolvedValueOnce(undefined as any)
            await permissionsApi.deleteProjectRole('p1', 'r1')
            expect(mockApiDelete).toHaveBeenCalledWith('/api/v1/projects/p1/roles/r1')
        })
    })

    describe('project role assignments', () => {
        it('grantProjectRole calls POST /api/v1/projects/{id}/members/{user}/roles', async () => {
            mockApiPost.mockResolvedValueOnce({ user_id: 'u1', project_id: 'p1', role_id: 'r1' })
            await permissionsApi.grantProjectRole('p1', 'u1', 'r1')
            expect(mockApiPost).toHaveBeenCalledWith(
                '/api/v1/projects/p1/members/u1/roles',
                { role_id: 'r1' },
            )
        })

        it('revokeProjectRole calls DELETE /api/v1/projects/{id}/members/{user}/roles/{role}', async () => {
            mockApiDelete.mockResolvedValueOnce(undefined as any)
            await permissionsApi.revokeProjectRole('p1', 'u1', 'r1')
            expect(mockApiDelete).toHaveBeenCalledWith('/api/v1/projects/p1/members/u1/roles/r1')
        })

        it('propagates errors', async () => {
            mockApiPost.mockRejectedValueOnce(new Error('forbidden'))
            await expect(
                permissionsApi.grantProjectRole('p1', 'u1', 'r1'),
            ).rejects.toThrow('forbidden')
        })
    })
})
