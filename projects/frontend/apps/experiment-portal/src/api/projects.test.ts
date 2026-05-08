import { describe, it, expect, vi, beforeEach } from 'vitest'

vi.mock('./client', () => ({
    apiGet: vi.fn(),
    apiClient: {
        get: vi.fn(),
        post: vi.fn(),
        put: vi.fn(),
        delete: vi.fn(),
    },
}))

import { projectsApi, usersApi } from './projects'
import { apiGet, apiClient } from './client'

const mockApiGet = vi.mocked(apiGet)
const mockClientGet = vi.mocked(apiClient.get)
const mockClientPost = vi.mocked(apiClient.post)
const mockClientPut = vi.mocked(apiClient.put)
const mockClientDelete = vi.mocked(apiClient.delete)

describe('projectsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    describe('list', () => {
        it('calls GET /projects and unwraps items into projects', async () => {
            const items = [{ id: 'p1', name: 'A' }, { id: 'p2', name: 'B' }]
            mockClientGet.mockResolvedValueOnce({ data: { items, total: 2 } } as any)

            const result = await projectsApi.list({ search: 'foo', limit: 50, offset: 0 })

            expect(mockClientGet).toHaveBeenCalledWith('/projects', {
                params: { search: 'foo', limit: 50, offset: 0 },
            })
            expect(result).toEqual({ projects: items, total: 2 })
        })

        it('handles missing items field with empty array', async () => {
            mockClientGet.mockResolvedValueOnce({ data: { total: 0 } } as any)
            const result = await projectsApi.list()
            expect(result).toEqual({ projects: [], total: 0 })
        })
    })

    describe('get', () => {
        it('calls GET /projects/{id}', async () => {
            mockClientGet.mockResolvedValueOnce({ data: { id: 'p1' } } as any)
            const result = await projectsApi.get('p1')
            expect(mockClientGet).toHaveBeenCalledWith('/projects/p1')
            expect(result).toEqual({ id: 'p1' })
        })
    })

    describe('create', () => {
        it('calls POST /projects with data', async () => {
            const data = { name: 'New Project' }
            mockClientPost.mockResolvedValueOnce({ data: { id: 'p1', ...data } } as any)
            const result = await projectsApi.create(data as any)
            expect(mockClientPost).toHaveBeenCalledWith('/projects', data)
            expect(result).toEqual({ id: 'p1', name: 'New Project' })
        })
    })

    describe('update', () => {
        it('calls PUT /projects/{id} with data', async () => {
            const data = { name: 'Updated' }
            mockClientPut.mockResolvedValueOnce({ data: { id: 'p1', ...data } } as any)
            await projectsApi.update('p1', data as any)
            expect(mockClientPut).toHaveBeenCalledWith('/projects/p1', data)
        })
    })

    describe('delete', () => {
        it('calls DELETE /projects/{id}', async () => {
            mockClientDelete.mockResolvedValueOnce({} as any)
            await projectsApi.delete('p1')
            expect(mockClientDelete).toHaveBeenCalledWith('/projects/p1')
        })
    })

    describe('listMembers', () => {
        it('calls GET /projects/{id}/members', async () => {
            mockClientGet.mockResolvedValueOnce({ data: { members: [], total: 0 } } as any)
            const result = await projectsApi.listMembers('p1')
            expect(mockClientGet).toHaveBeenCalledWith('/projects/p1/members')
            expect(result).toEqual({ members: [], total: 0 })
        })
    })

    describe('addMember', () => {
        it('calls POST /projects/{id}/members with data', async () => {
            const data = { user_id: 'u1', role: 'editor' }
            mockClientPost.mockResolvedValueOnce({ data: { id: 'm1', ...data } } as any)
            await projectsApi.addMember('p1', data as any)
            expect(mockClientPost).toHaveBeenCalledWith('/projects/p1/members', data)
        })
    })

    describe('removeMember', () => {
        it('calls DELETE /projects/{id}/members/{user}', async () => {
            mockClientDelete.mockResolvedValueOnce({} as any)
            await projectsApi.removeMember('p1', 'u1')
            expect(mockClientDelete).toHaveBeenCalledWith('/projects/p1/members/u1')
        })
    })

    describe('updateMemberRole', () => {
        it('calls PUT /projects/{id}/members/{user}/role with data', async () => {
            const data = { role: 'owner' }
            mockClientPut.mockResolvedValueOnce({ data: { user_id: 'u1', ...data } } as any)
            await projectsApi.updateMemberRole('p1', 'u1', data as any)
            expect(mockClientPut).toHaveBeenCalledWith('/projects/p1/members/u1/role', data)
        })

        it('propagates errors', async () => {
            mockClientPut.mockRejectedValueOnce(new Error('forbidden'))
            await expect(
                projectsApi.updateMemberRole('p1', 'u1', { role: 'owner' } as any),
            ).rejects.toThrow('forbidden')
        })
    })
})

describe('usersApi.search', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('calls GET /api/v1/users/search and wraps array in { users }', async () => {
        const users = [
            { id: 'u1', username: 'alice', email: 'alice@example.com' },
            { id: 'u2', username: 'bob', email: 'bob@example.com' },
        ]
        mockApiGet.mockResolvedValueOnce(users)

        const result = await usersApi.search({ q: 'al', exclude_project_id: 'p1' })

        expect(mockApiGet).toHaveBeenCalledWith('/api/v1/users/search', {
            params: { q: 'al', exclude_project_id: 'p1' },
        })
        expect(result).toEqual({ users })
    })

    it('returns empty users array when API returns non-array', async () => {
        mockApiGet.mockResolvedValueOnce({} as any)
        const result = await usersApi.search({ q: 'al' })
        expect(result).toEqual({ users: [] })
    })
})
