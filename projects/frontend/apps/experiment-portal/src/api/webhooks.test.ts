import { describe, it, expect, vi, beforeEach } from 'vitest'

const { mockAxiosInstance, mockCreate } = vi.hoisted(() => {
    const instance = {
        get: vi.fn(),
        post: vi.fn(),
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

import { webhooksApi } from './webhooks'
import { setActiveProjectId } from '../utils/activeProject'

const PROJECT_ID = 'project-1'

describe('webhooksApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        setActiveProjectId(PROJECT_ID)
    })

    it('list calls GET /api/v1/webhooks with paging params', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({
            data: { webhooks: [], total: 0 },
        })

        await webhooksApi.list({ page: 2, page_size: 25 })

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/webhooks',
            expect.objectContaining({
                params: expect.objectContaining({
                    page: 2,
                    page_size: 25,
                    project_id: PROJECT_ID,
                }),
            })
        )
    })

    it('create POSTs payload to /api/v1/webhooks', async () => {
        const payload = { url: 'https://hook.example/cb', events: ['run.completed'] }
        mockAxiosInstance.post.mockResolvedValueOnce({
            data: { id: 'wh-1', ...payload, secret: 'shh' },
        })

        const result = await webhooksApi.create(payload as any)

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/webhooks',
            payload,
            expect.any(Object)
        )
        expect(result).toMatchObject({ id: 'wh-1' })
    })

    it('delete DELETEs /api/v1/webhooks/{id}', async () => {
        mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

        await webhooksApi.delete('wh-1')

        expect(mockAxiosInstance.delete).toHaveBeenCalledWith(
            '/api/v1/webhooks/wh-1',
            expect.any(Object)
        )
    })

    it('listDeliveries calls GET /api/v1/webhooks/deliveries with status filter', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({
            data: { deliveries: [], total: 0 },
        })

        await webhooksApi.listDeliveries({ status: 'failed', page: 1, page_size: 50 })

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/webhooks/deliveries',
            expect.objectContaining({
                params: expect.objectContaining({
                    status: 'failed',
                    page: 1,
                    page_size: 50,
                    project_id: PROJECT_ID,
                }),
            })
        )
    })

    it('retryDelivery POSTs to /api/v1/webhooks/deliveries/{id}:retry', async () => {
        mockAxiosInstance.post.mockResolvedValueOnce({ data: {} })

        await webhooksApi.retryDelivery('del-1')

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/webhooks/deliveries/del-1:retry',
            undefined,
            expect.any(Object)
        )
    })

    it('propagates errors from list', async () => {
        mockAxiosInstance.get.mockRejectedValueOnce(new Error('boom'))
        await expect(webhooksApi.list()).rejects.toThrow('boom')
    })
})
