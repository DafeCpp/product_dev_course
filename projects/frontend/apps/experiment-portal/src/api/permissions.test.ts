import { describe, it, expect, vi, beforeEach } from 'vitest'

const { mockAxiosInstance, mockCreate } = vi.hoisted(() => {
    const instance = {
        get: vi.fn(),
        post: vi.fn(),
        patch: vi.fn(),
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

import { permissionsApi } from './permissions'
import { setActiveProjectId } from '../utils/activeProject'

describe('permissionsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        setActiveProjectId('project-1')
    })

    it('listPermissions calls GET /api/v1/permissions', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: [{ name: 'experiments.read' }] })

        const result = await permissionsApi.listPermissions()

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/permissions',
            expect.any(Object)
        )
        expect(result).toEqual([{ name: 'experiments.read' }])
    })

    it('getEffectivePermissions GETs /users/{id}/effective-permissions with project_id when provided', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: { permissions: [] } })

        await permissionsApi.getEffectivePermissions('user-1', 'project-9')

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/users/user-1/effective-permissions',
            expect.objectContaining({
                params: expect.objectContaining({ project_id: 'project-9' }),
            })
        )
    })

    it('getEffectivePermissions falls back to active project_id when not provided', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: { permissions: [] } })

        await permissionsApi.getEffectivePermissions('user-1')

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/users/user-1/effective-permissions',
            expect.objectContaining({
                params: expect.objectContaining({ project_id: 'project-1' }),
            })
        )
    })

    it('listSystemRoles GETs /api/v1/system-roles', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: [{ id: 'r1', name: 'admin' }] })

        const result = await permissionsApi.listSystemRoles()

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/system-roles',
            expect.any(Object)
        )
        expect(result).toEqual([{ id: 'r1', name: 'admin' }])
    })

    it('createSystemRole POSTs payload', async () => {
        const payload = { name: 'admin', description: 'desc', permissions: ['p1'] }
        mockAxiosInstance.post.mockResolvedValueOnce({ data: { id: 'r1', ...payload } })

        await permissionsApi.createSystemRole(payload)

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/system-roles',
            payload,
            expect.any(Object)
        )
    })

    it('updateSystemRole PATCHes /api/v1/system-roles/{id}', async () => {
        const payload = { name: 'admin-2' }
        mockAxiosInstance.patch.mockResolvedValueOnce({ data: { id: 'r1', name: 'admin-2' } })

        await permissionsApi.updateSystemRole('r1', payload)

        expect(mockAxiosInstance.patch).toHaveBeenCalledWith(
            '/api/v1/system-roles/r1',
            payload,
            expect.any(Object)
        )
    })

    it('deleteSystemRole DELETEs /api/v1/system-roles/{id}', async () => {
        mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

        await permissionsApi.deleteSystemRole('r1')

        expect(mockAxiosInstance.delete).toHaveBeenCalledWith(
            '/api/v1/system-roles/r1',
            expect.any(Object)
        )
    })

    it('grantSystemRole POSTs body when given a string roleId', async () => {
        mockAxiosInstance.post.mockResolvedValueOnce({ data: { user_id: 'u1', role_id: 'r1' } })

        await permissionsApi.grantSystemRole('u1', 'r1')

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/users/u1/system-roles',
            { role_id: 'r1' },
            expect.any(Object)
        )
    })

    it('grantSystemRole includes expires_at when given as 3rd arg', async () => {
        mockAxiosInstance.post.mockResolvedValueOnce({ data: { user_id: 'u1', role_id: 'r1' } })

        await permissionsApi.grantSystemRole('u1', 'r1', '2030-01-01T00:00:00Z')

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/users/u1/system-roles',
            { role_id: 'r1', expires_at: '2030-01-01T00:00:00Z' },
            expect.any(Object)
        )
    })

    it('grantSystemRole forwards an object body unchanged', async () => {
        mockAxiosInstance.post.mockResolvedValueOnce({ data: {} })

        await permissionsApi.grantSystemRole('u1', { role_id: 'r1', expires_at: '2030-01-01' })

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/users/u1/system-roles',
            { role_id: 'r1', expires_at: '2030-01-01' },
            expect.any(Object)
        )
    })

    it('revokeSystemRole DELETEs /users/{uid}/system-roles/{rid}', async () => {
        mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

        await permissionsApi.revokeSystemRole('u1', 'r1')

        expect(mockAxiosInstance.delete).toHaveBeenCalledWith(
            '/api/v1/users/u1/system-roles/r1',
            expect.any(Object)
        )
    })

    it('listProjectRoles GETs /projects/{pid}/roles', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: [] })

        await permissionsApi.listProjectRoles('p1')

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/projects/p1/roles',
            expect.any(Object)
        )
    })

    it('createProjectRole POSTs payload to /projects/{pid}/roles', async () => {
        const payload = { name: 'editor', permissions: ['exp.write'] }
        mockAxiosInstance.post.mockResolvedValueOnce({ data: { id: 'r1', ...payload } })

        await permissionsApi.createProjectRole('p1', payload)

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/projects/p1/roles',
            payload,
            expect.any(Object)
        )
    })

    it('updateProjectRole PATCHes /projects/{pid}/roles/{rid}', async () => {
        mockAxiosInstance.patch.mockResolvedValueOnce({ data: { id: 'r1' } })

        await permissionsApi.updateProjectRole('p1', 'r1', { name: 'editor-2' })

        expect(mockAxiosInstance.patch).toHaveBeenCalledWith(
            '/api/v1/projects/p1/roles/r1',
            { name: 'editor-2' },
            expect.any(Object)
        )
    })

    it('deleteProjectRole DELETEs /projects/{pid}/roles/{rid}', async () => {
        mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

        await permissionsApi.deleteProjectRole('p1', 'r1')

        expect(mockAxiosInstance.delete).toHaveBeenCalledWith(
            '/api/v1/projects/p1/roles/r1',
            expect.any(Object)
        )
    })

    it('grantProjectRole POSTs role_id to /projects/{pid}/members/{uid}/roles', async () => {
        mockAxiosInstance.post.mockResolvedValueOnce({ data: { user_id: 'u1', role_id: 'r1' } })

        await permissionsApi.grantProjectRole('p1', 'u1', 'r1')

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/projects/p1/members/u1/roles',
            { role_id: 'r1' },
            expect.any(Object)
        )
    })

    it('revokeProjectRole DELETEs /projects/{pid}/members/{uid}/roles/{rid}', async () => {
        mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

        await permissionsApi.revokeProjectRole('p1', 'u1', 'r1')

        expect(mockAxiosInstance.delete).toHaveBeenCalledWith(
            '/api/v1/projects/p1/members/u1/roles/r1',
            expect.any(Object)
        )
    })
})
