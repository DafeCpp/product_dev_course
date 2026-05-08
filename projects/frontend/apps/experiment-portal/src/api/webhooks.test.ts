import { describe, it, expect, vi, beforeEach } from 'vitest'

vi.mock('./client', () => ({
    apiGet: vi.fn(),
    apiPost: vi.fn(),
    apiDelete: vi.fn(),
}))

import { webhooksApi } from './webhooks'
import { apiGet, apiPost, apiDelete } from './client'

const mockApiGet = vi.mocked(apiGet)
const mockApiPost = vi.mocked(apiPost)
const mockApiDelete = vi.mocked(apiDelete)

describe('webhooksApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    describe('list', () => {
        it('calls GET /api/v1/webhooks with pagination', async () => {
            mockApiGet.mockResolvedValueOnce({ webhooks: [], total: 0 })
            await webhooksApi.list({ page: 2, page_size: 50 })
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/webhooks', {
                params: { page: 2, page_size: 50 },
            })
        })

        it('passes undefined params when no pagination', async () => {
            mockApiGet.mockResolvedValueOnce({ webhooks: [], total: 0 })
            await webhooksApi.list()
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/webhooks', { params: undefined })
        })
    })

    describe('create', () => {
        it('calls POST /api/v1/webhooks with subscription data', async () => {
            const data = {
                target_url: 'https://example.com/hook',
                event_types: ['run.completed'],
                secret: 's3cr3t',
            }
            const mockWebhook = { id: 'wh-1', ...data }
            mockApiPost.mockResolvedValueOnce(mockWebhook)

            const result = await webhooksApi.create(data as any)

            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/webhooks', data)
            expect(result).toEqual(mockWebhook)
        })

        it('propagates validation errors', async () => {
            mockApiPost.mockRejectedValueOnce(new Error('invalid url'))
            await expect(webhooksApi.create({} as any)).rejects.toThrow('invalid url')
        })
    })

    describe('delete', () => {
        it('calls DELETE /api/v1/webhooks/{id}', async () => {
            mockApiDelete.mockResolvedValueOnce(undefined as any)
            await webhooksApi.delete('wh-1')
            expect(mockApiDelete).toHaveBeenCalledWith('/api/v1/webhooks/wh-1')
        })
    })

    describe('listDeliveries', () => {
        it('calls GET /api/v1/webhooks/deliveries with status filter', async () => {
            mockApiGet.mockResolvedValueOnce({ deliveries: [], total: 0 })
            await webhooksApi.listDeliveries({ status: 'failed', page: 1, page_size: 25 })
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/webhooks/deliveries', {
                params: { status: 'failed', page: 1, page_size: 25 },
            })
        })
    })

    describe('retryDelivery', () => {
        it('calls POST /api/v1/webhooks/deliveries/{id}:retry', async () => {
            mockApiPost.mockResolvedValueOnce(undefined as any)
            await webhooksApi.retryDelivery('del-1')
            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/webhooks/deliveries/del-1:retry')
        })
    })
})
