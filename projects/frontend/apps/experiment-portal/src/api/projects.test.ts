import { describe, it, expect, vi, beforeEach } from 'vitest'

const { mockAxiosInstance, mockCreate } = vi.hoisted(() => {
    const instance = {
        get: vi.fn(),
        post: vi.fn(),
        put: vi.fn(),
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

import { projectsApi, usersApi } from './projects'

describe('projectsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    describe('list', () => {
        it('calls GET /projects with params and unwraps {items,total}', async () => {
            const items = [{ id: 'p1', name: 'P1' }, { id: 'p2', name: 'P2' }]
            mockAxiosInstance.get.mockResolvedValueOnce({ data: { items, total: 2 } })

            const result = await projectsApi.list({ search: 'foo', role: 'owner', limit: 10, offset: 0 })

            expect(mockAxiosInstance.get).toHaveBeenCalledWith('/projects', {
                params: { search: 'foo', role: 'owner', limit: 10, offset: 0 },
            })
            expect(result).toEqual({ projects: items, total: 2 })
        })

        it('returns an empty list when items is missing', async () => {
            mockAxiosInstance.get.mockResolvedValueOnce({ data: { total: 0 } })

            const result = await projectsApi.list()

            expect(result).toEqual({ projects: [], total: 0 })
        })
    })

    describe('get', () => {
        it('calls GET /projects/{id}', async () => {
            const project = { id: 'p1', name: 'P1' }
            mockAxiosInstance.get.mockResolvedValueOnce({ data: project })

            const result = await projectsApi.get('p1')

            expect(mockAxiosInstance.get).toHaveBeenCalledWith('/projects/p1')
            expect(result).toEqual(project)
        })
    })

    describe('create', () => {
        it('calls POST /projects with payload', async () => {
            const payload = { name: 'P1', description: 'desc' }
            const created = { id: 'p1', ...payload }
            mockAxiosInstance.post.mockResolvedValueOnce({ data: created })

            const result = await projectsApi.create(payload as any)

            expect(mockAxiosInstance.post).toHaveBeenCalledWith('/projects', payload)
            expect(result).toEqual(created)
        })
    })

    describe('update', () => {
        it('calls PUT /projects/{id} with payload', async () => {
            const upd = { name: 'New' }
            const project = { id: 'p1', name: 'New' }
            mockAxiosInstance.put.mockResolvedValueOnce({ data: project })

            const result = await projectsApi.update('p1', upd as any)

            expect(mockAxiosInstance.put).toHaveBeenCalledWith('/projects/p1', upd)
            expect(result).toEqual(project)
        })
    })

    describe('delete', () => {
        it('calls DELETE /projects/{id}', async () => {
            mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

            await projectsApi.delete('p1')

            expect(mockAxiosInstance.delete).toHaveBeenCalledWith('/projects/p1')
        })
    })

    describe('listMembers', () => {
        it('calls GET /projects/{id}/members', async () => {
            const members = { members: [], total: 0 }
            mockAxiosInstance.get.mockResolvedValueOnce({ data: members })

            const result = await projectsApi.listMembers('p1')

            expect(mockAxiosInstance.get).toHaveBeenCalledWith('/projects/p1/members')
            expect(result).toEqual(members)
        })
    })

    describe('addMember', () => {
        it('calls POST /projects/{id}/members with payload', async () => {
            const payload = { user_id: 'u1', role: 'editor' }
            const member = { id: 'm1', user_id: 'u1', role: 'editor' }
            mockAxiosInstance.post.mockResolvedValueOnce({ data: member })

            const result = await projectsApi.addMember('p1', payload as any)

            expect(mockAxiosInstance.post).toHaveBeenCalledWith(
                '/projects/p1/members',
                payload
            )
            expect(result).toEqual(member)
        })
    })

    describe('removeMember', () => {
        it('calls DELETE /projects/{pid}/members/{uid}', async () => {
            mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

            await projectsApi.removeMember('p1', 'u1')

            expect(mockAxiosInstance.delete).toHaveBeenCalledWith('/projects/p1/members/u1')
        })
    })

    describe('updateMemberRole', () => {
        it('calls PUT /projects/{pid}/members/{uid}/role', async () => {
            const payload = { role: 'viewer' }
            const member = { id: 'm1', user_id: 'u1', role: 'viewer' }
            mockAxiosInstance.put.mockResolvedValueOnce({ data: member })

            const result = await projectsApi.updateMemberRole('p1', 'u1', payload as any)

            expect(mockAxiosInstance.put).toHaveBeenCalledWith(
                '/projects/p1/members/u1/role',
                payload
            )
            expect(result).toEqual(member)
        })
    })
})

describe('usersApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    describe('search', () => {
        it('calls GET /api/v1/users/search and wraps array in {users}', async () => {
            const users = [{ id: 'u1', username: 'alice', email: 'a@x' }]
            mockAxiosInstance.get.mockResolvedValueOnce({ data: users })

            const result = await usersApi.search({ q: 'al', exclude_project_id: 'p1' })

            expect(mockAxiosInstance.get).toHaveBeenCalledWith(
                '/api/v1/users/search',
                expect.objectContaining({
                    params: expect.objectContaining({ q: 'al', exclude_project_id: 'p1' }),
                })
            )
            expect(result).toEqual({ users })
        })

        it('returns {users: []} when API returns a non-array', async () => {
            mockAxiosInstance.get.mockResolvedValueOnce({ data: null })

            const result = await usersApi.search({ q: 'al' })

            expect(result).toEqual({ users: [] })
        })
    })
})
