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

import { artifactsApi, runSensorsApi } from './artifacts'
import { setActiveProjectId } from '../utils/activeProject'

const PROJECT_ID = 'project-1'

describe('artifactsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        setActiveProjectId(PROJECT_ID)
    })

    it('list calls GET /api/v1/runs/{id}/artifacts with type/limit/offset', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: { artifacts: [], total: 0 } })

        await artifactsApi.list('run-1', { type: 'model', limit: 50, offset: 0 })

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/artifacts',
            expect.objectContaining({
                params: expect.objectContaining({
                    type: 'model',
                    limit: 50,
                    offset: 0,
                    project_id: PROJECT_ID,
                }),
            })
        )
    })

    it('create POSTs payload to /api/v1/runs/{id}/artifacts', async () => {
        const data = { name: 'model.pt', type: 'model', size_bytes: 1024 }
        mockAxiosInstance.post.mockResolvedValueOnce({ data: { id: 'a1', ...data } })

        const result = await artifactsApi.create('run-1', data as any)

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/artifacts',
            data,
            expect.any(Object)
        )
        expect(result).toMatchObject({ id: 'a1' })
    })

    it('delete DELETEs /api/v1/artifacts/{id}', async () => {
        mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

        await artifactsApi.delete('a1')

        expect(mockAxiosInstance.delete).toHaveBeenCalledWith(
            '/api/v1/artifacts/a1',
            expect.any(Object)
        )
    })

    it('approve POSTs to /artifacts/{id}/approve', async () => {
        mockAxiosInstance.post.mockResolvedValueOnce({ data: { id: 'a1', approved: true } })

        await artifactsApi.approve('a1')

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/artifacts/a1/approve',
            {},
            expect.any(Object)
        )
    })

    it('requestUploadUrl POSTs presign request to /artifacts/upload-url', async () => {
        const payload = {
            filename: 'model.pt',
            content_type: 'application/octet-stream',
            type: 'model',
            size_bytes: 1024,
        }
        mockAxiosInstance.post.mockResolvedValueOnce({
            data: { upload_url: 'https://s3/u', artifact_id: 'a1', s3_key: 'k' },
        })

        const result = await artifactsApi.requestUploadUrl('run-1', payload)

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/artifacts/upload-url',
            payload,
            expect.any(Object)
        )
        expect(result.upload_url).toBe('https://s3/u')
    })

    it('getDownloadUrl GETs /artifacts/{id}/download-url', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({
            data: { download_url: 'https://s3/d', expires_in: 60 },
        })

        const result = await artifactsApi.getDownloadUrl('a1')

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/artifacts/a1/download-url',
            expect.any(Object)
        )
        expect(result.expires_in).toBe(60)
    })
})

describe('runSensorsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        setActiveProjectId(PROJECT_ID)
    })

    it('list calls GET /api/v1/runs/{id}/sensors', async () => {
        mockAxiosInstance.get.mockResolvedValueOnce({ data: { sensors: [] } })

        await runSensorsApi.list('run-1')

        expect(mockAxiosInstance.get).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/sensors',
            expect.objectContaining({
                params: expect.objectContaining({ project_id: PROJECT_ID }),
            })
        )
    })

    it('attach POSTs to /api/v1/runs/{rid}/sensors/{sid}', async () => {
        mockAxiosInstance.post.mockResolvedValueOnce({ data: { id: 'rs1' } })

        await runSensorsApi.attach('run-1', 'sensor-1')

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/sensors/sensor-1',
            {},
            expect.objectContaining({
                params: expect.objectContaining({ project_id: PROJECT_ID }),
            })
        )
    })

    it('detach DELETEs /api/v1/runs/{rid}/sensors/{sid}', async () => {
        mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

        await runSensorsApi.detach('run-1', 'sensor-1')

        expect(mockAxiosInstance.delete).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/sensors/sensor-1',
            expect.any(Object)
        )
    })

    it('attach with explicit project_id forwards it (overrides active)', async () => {
        mockAxiosInstance.post.mockResolvedValueOnce({ data: { id: 'rs1' } })

        await runSensorsApi.attach('run-1', 'sensor-1', { project_id: 'override-project' })

        expect(mockAxiosInstance.post).toHaveBeenCalledWith(
            '/api/v1/runs/run-1/sensors/sensor-1',
            {},
            expect.objectContaining({
                params: expect.objectContaining({ project_id: 'override-project' }),
            })
        )
    })
})
