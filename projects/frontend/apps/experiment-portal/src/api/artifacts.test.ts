import { describe, it, expect, vi, beforeEach } from 'vitest'

vi.mock('./client', () => ({
    apiGet: vi.fn(),
    apiPost: vi.fn(),
    apiDelete: vi.fn(),
}))

import { artifactsApi, runSensorsApi } from './artifacts'
import { apiGet, apiPost, apiDelete } from './client'

const mockApiGet = vi.mocked(apiGet)
const mockApiPost = vi.mocked(apiPost)
const mockApiDelete = vi.mocked(apiDelete)

describe('artifactsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    describe('list', () => {
        it('calls GET /api/v1/runs/{id}/artifacts with params', async () => {
            const mockResponse = { artifacts: [], total: 0 }
            mockApiGet.mockResolvedValueOnce(mockResponse)

            const result = await artifactsApi.list('run-1', { type: 'image', limit: 10, offset: 0 })

            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/runs/run-1/artifacts', {
                params: { type: 'image', limit: 10, offset: 0 },
            })
            expect(result).toEqual(mockResponse)
        })

        it('passes undefined params when no filters provided', async () => {
            mockApiGet.mockResolvedValueOnce({ artifacts: [], total: 0 })
            await artifactsApi.list('run-1')
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/runs/run-1/artifacts', { params: undefined })
        })
    })

    describe('create', () => {
        it('calls POST /api/v1/runs/{id}/artifacts with data', async () => {
            const data = { type: 'image' as const, filename: 'test.png', size_bytes: 1000 }
            const mockArtifact = { id: 'artifact-1', ...data }
            mockApiPost.mockResolvedValueOnce(mockArtifact)

            const result = await artifactsApi.create('run-1', data as any)

            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/runs/run-1/artifacts', data)
            expect(result).toEqual(mockArtifact)
        })
    })

    describe('delete', () => {
        it('calls DELETE /api/v1/artifacts/{id}', async () => {
            mockApiDelete.mockResolvedValueOnce(undefined as any)
            await artifactsApi.delete('artifact-1')
            expect(mockApiDelete).toHaveBeenCalledWith('/api/v1/artifacts/artifact-1')
        })

        it('propagates errors', async () => {
            mockApiDelete.mockRejectedValueOnce(new Error('forbidden'))
            await expect(artifactsApi.delete('artifact-1')).rejects.toThrow('forbidden')
        })
    })

    describe('approve', () => {
        it('calls POST /api/v1/artifacts/{id}/approve', async () => {
            const mockArtifact = { id: 'artifact-1', approved: true }
            mockApiPost.mockResolvedValueOnce(mockArtifact)

            const result = await artifactsApi.approve('artifact-1')

            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/artifacts/artifact-1/approve', {})
            expect(result).toEqual(mockArtifact)
        })
    })

    describe('requestUploadUrl', () => {
        it('calls POST /api/v1/runs/{id}/artifacts/upload-url with metadata', async () => {
            const data = {
                filename: 'data.csv',
                content_type: 'text/csv',
                type: 'dataset',
                size_bytes: 2048,
                metadata: { source: 'test' },
            }
            const mockResponse = { upload_url: 'https://s3/upload', artifact_id: 'a1', s3_key: 'k1' }
            mockApiPost.mockResolvedValueOnce(mockResponse)

            const result = await artifactsApi.requestUploadUrl('run-1', data)

            expect(mockApiPost).toHaveBeenCalledWith('/api/v1/runs/run-1/artifacts/upload-url', data)
            expect(result).toEqual(mockResponse)
        })
    })

    describe('getDownloadUrl', () => {
        it('calls GET /api/v1/artifacts/{id}/download-url', async () => {
            const mockResponse = { download_url: 'https://s3/download', expires_in: 3600 }
            mockApiGet.mockResolvedValueOnce(mockResponse)

            const result = await artifactsApi.getDownloadUrl('artifact-1')

            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/artifacts/artifact-1/download-url')
            expect(result).toEqual(mockResponse)
        })
    })
})

describe('runSensorsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    describe('list', () => {
        it('calls GET /api/v1/runs/{id}/sensors with project_id', async () => {
            mockApiGet.mockResolvedValueOnce({ sensors: [] })
            await runSensorsApi.list('run-1', { project_id: 'p1' })
            expect(mockApiGet).toHaveBeenCalledWith('/api/v1/runs/run-1/sensors', {
                params: { project_id: 'p1' },
            })
        })
    })

    describe('attach', () => {
        it('calls POST /api/v1/runs/{id}/sensors/{sensor_id}', async () => {
            const mockSensor = { sensor_id: 'sensor-1', run_id: 'run-1' }
            mockApiPost.mockResolvedValueOnce(mockSensor)

            const result = await runSensorsApi.attach('run-1', 'sensor-1', { project_id: 'p1' })

            expect(mockApiPost).toHaveBeenCalledWith(
                '/api/v1/runs/run-1/sensors/sensor-1',
                {},
                { params: { project_id: 'p1' } },
            )
            expect(result).toEqual(mockSensor)
        })
    })

    describe('detach', () => {
        it('calls DELETE /api/v1/runs/{id}/sensors/{sensor_id}', async () => {
            mockApiDelete.mockResolvedValueOnce(undefined as any)
            await runSensorsApi.detach('run-1', 'sensor-1', { project_id: 'p1' })
            expect(mockApiDelete).toHaveBeenCalledWith(
                '/api/v1/runs/run-1/sensors/sensor-1',
                { params: { project_id: 'p1' } },
            )
        })
    })
})
