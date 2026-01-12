import { describe, it, expect, vi, beforeEach } from 'vitest'

// Используем vi.hoisted для правильного порядка инициализации
const { mockAxiosInstance, mockCreate } = vi.hoisted(() => {
    const instance = {
        post: vi.fn(),
        get: vi.fn(),
        patch: vi.fn(),
        delete: vi.fn(),
        interceptors: {
            request: {
                use: vi.fn(),
            },
            response: {
                use: vi.fn(),
            },
        },
    }
    const create = vi.fn(() => instance)
    return { mockAxiosInstance: instance, mockCreate: create }
})

// Мокаем axios ДО импорта client
vi.mock('axios', () => {
    return {
        default: {
            create: mockCreate,
        },
    }
})

// Импортируем после мока
import { sensorsApi } from './client'

describe('sensorsApi', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    describe('list', () => {
        it('calls GET /api/v1/sensors with params', async () => {
            const mockResponse = {
                data: {
                    sensors: [],
                    total: 0,
                    page: 1,
                    page_size: 20,
                },
            }
            mockAxiosInstance.get.mockResolvedValueOnce(mockResponse)

            const result = await sensorsApi.list({
                project_id: 'project-1',
                status: 'active',
                page: 1,
                page_size: 20,
            })

            expect(mockAxiosInstance.get).toHaveBeenCalledWith('/api/v1/sensors', {
                params: {
                    project_id: 'project-1',
                    status: 'active',
                    page: 1,
                    page_size: 20,
                },
            })
            expect(result).toEqual(mockResponse.data)
        })
    })

    describe('get', () => {
        it('calls GET /api/v1/sensors/{id}', async () => {
            const mockSensor = {
                id: 'sensor-1',
                project_id: 'project-1',
                name: 'Test Sensor',
                type: 'temperature',
                input_unit: 'V',
                display_unit: '°C',
                status: 'active',
                created_at: '2024-01-01T00:00:00Z',
                updated_at: '2024-01-01T00:00:00Z',
            }
            mockAxiosInstance.get.mockResolvedValueOnce({ data: mockSensor })

            const result = await sensorsApi.get('sensor-1')

            expect(mockAxiosInstance.get).toHaveBeenCalledWith('/api/v1/sensors/sensor-1', {
                params: undefined,
            })
            expect(result).toEqual(mockSensor)
        })
    })

    describe('create', () => {
        it('calls POST /api/v1/sensors with sensor data', async () => {
            const sensorData = {
                project_id: 'project-1',
                name: 'Test Sensor',
                type: 'temperature',
                input_unit: 'V',
                display_unit: '°C',
            }
            const mockResponse = {
                data: {
                    sensor: {
                        id: 'sensor-1',
                        ...sensorData,
                        status: 'registering',
                        created_at: '2024-01-01T00:00:00Z',
                        updated_at: '2024-01-01T00:00:00Z',
                    },
                    token: 'test-token-12345',
                },
            }
            mockAxiosInstance.post.mockResolvedValueOnce(mockResponse)

            const result = await sensorsApi.create(sensorData)

            expect(mockAxiosInstance.post).toHaveBeenCalledWith('/api/v1/sensors', sensorData, {
                params: { project_id: sensorData.project_id },
            })
            expect(result).toEqual(mockResponse.data)
        })
    })

    describe('update', () => {
        it('calls PATCH /api/v1/sensors/{id} with update data', async () => {
            const updateData = {
                name: 'Updated Sensor',
                calibration_notes: 'Updated notes',
            }
            const mockSensor = {
                id: 'sensor-1',
                project_id: 'project-1',
                name: 'Updated Sensor',
                type: 'temperature',
                input_unit: 'V',
                display_unit: '°C',
                status: 'active',
                created_at: '2024-01-01T00:00:00Z',
                updated_at: '2024-01-01T00:00:00Z',
            }
            mockAxiosInstance.patch.mockResolvedValueOnce({ data: mockSensor })

            const result = await sensorsApi.update('sensor-1', updateData)

            expect(mockAxiosInstance.patch).toHaveBeenCalledWith(
                '/api/v1/sensors/sensor-1',
                updateData,
                { params: undefined }
            )
            expect(result).toEqual(mockSensor)
        })
    })

    describe('delete', () => {
        it('calls DELETE /api/v1/sensors/{id}', async () => {
            mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

            await sensorsApi.delete('sensor-1')

            expect(mockAxiosInstance.delete).toHaveBeenCalledWith('/api/v1/sensors/sensor-1', {
                params: undefined,
            })
        })
    })

    describe('rotateToken', () => {
        it('calls POST /api/v1/sensors/{id}/rotate-token', async () => {
            const mockResponse = {
                data: {
                    sensor: {
                        id: 'sensor-1',
                        project_id: 'project-1',
                        name: 'Test Sensor',
                        type: 'temperature',
                        input_unit: 'V',
                        display_unit: '°C',
                        status: 'active',
                        created_at: '2024-01-01T00:00:00Z',
                        updated_at: '2024-01-01T00:00:00Z',
                    },
                    token: 'new-token-67890',
                },
            }
            mockAxiosInstance.post.mockResolvedValueOnce(mockResponse)

            const result = await sensorsApi.rotateToken('sensor-1')

            expect(mockAxiosInstance.post).toHaveBeenCalledWith(
                '/api/v1/sensors/sensor-1/rotate-token',
                {},
                { params: undefined }
            )
            expect(result).toEqual(mockResponse.data)
        })
    })
})

