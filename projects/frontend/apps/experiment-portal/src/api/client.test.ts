import { describe, it, expect, vi, beforeEach } from 'vitest'

// Используем vi.hoisted для правильного порядка инициализации
const { mockAxiosInstance, mockCreate, mockAxiosPost } = vi.hoisted(() => {
    const instance = {
        get: vi.fn(),
        post: vi.fn(),
        patch: vi.fn(),
        delete: vi.fn(),
        interceptors: {
            response: {
                use: vi.fn(),
            },
        },
    }
    const create = vi.fn(() => instance)
    const axiosPost = vi.fn()
    return { mockAxiosInstance: instance, mockCreate: create, mockAxiosPost: axiosPost }
})

// Мокаем axios ДО импорта client
vi.mock('axios', () => {
    return {
        default: {
            create: mockCreate,
            post: mockAxiosPost,
        },
    }
})

// Мокаем window.location
const mockLocation = {
    href: '',
}
Object.defineProperty(window, 'location', {
    value: mockLocation,
    writable: true,
})

// Импортируем после мока
import { experimentsApi, runsApi } from './client'

// Сохраняем количество вызовов interceptor ДО первого beforeEach
// Interceptor устанавливается при импорте модуля client.ts
const initialInterceptorCallCount = mockAxiosInstance.interceptors.response.use.mock.calls.length

describe('API Client', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockLocation.href = ''
        // Сбрасываем interceptor
        const interceptor = mockAxiosInstance.interceptors.response.use.mock.calls[0]
        if (interceptor) {
            const errorHandler = interceptor[1]
            // Создаем mock error для тестирования interceptor
            const mockError = {
                response: { status: 401 },
                config: { _retry: false },
            }
            errorHandler(mockError).catch(() => {})
        }
    })

    describe('experimentsApi', () => {
        describe('list', () => {
            it('calls GET /api/v1/experiments with params', async () => {
                const mockResponse = {
                    data: {
                        experiments: [],
                        total: 0,
                        page: 1,
                        page_size: 20,
                    },
                }
                mockAxiosInstance.get.mockResolvedValueOnce(mockResponse)

                const result = await experimentsApi.list({
                    project_id: 'project-123',
                    status: 'running',
                    page: 1,
                    page_size: 20,
                })

                expect(mockAxiosInstance.get).toHaveBeenCalledWith('/api/v1/experiments', {
                    params: {
                        project_id: 'project-123',
                        status: 'running',
                        page: 1,
                        page_size: 20,
                    },
                })
                expect(result).toEqual(mockResponse.data)
            })

            it('calls GET /api/v1/experiments without params', async () => {
                const mockResponse = {
                    data: {
                        experiments: [],
                        total: 0,
                        page: 1,
                        page_size: 20,
                    },
                }
                mockAxiosInstance.get.mockResolvedValueOnce(mockResponse)

                const result = await experimentsApi.list()

                expect(mockAxiosInstance.get).toHaveBeenCalledWith('/api/v1/experiments', {
                    params: undefined,
                })
                expect(result).toEqual(mockResponse.data)
            })
        })

        describe('get', () => {
            it('calls GET /api/v1/experiments/:id', async () => {
                const mockExperiment = {
                    id: 'exp-123',
                    project_id: 'project-123',
                    name: 'Test Experiment',
                    status: 'created',
                    tags: [],
                    metadata: {},
                    created_by: 'user-1',
                    created_at: '2024-01-01T00:00:00Z',
                    updated_at: '2024-01-01T00:00:00Z',
                }
                mockAxiosInstance.get.mockResolvedValueOnce({ data: mockExperiment })

                const result = await experimentsApi.get('exp-123')

                expect(mockAxiosInstance.get).toHaveBeenCalledWith('/api/v1/experiments/exp-123')
                expect(result).toEqual(mockExperiment)
            })
        })

        describe('create', () => {
            it('calls POST /api/v1/experiments with data', async () => {
                const createData = {
                    project_id: 'project-123',
                    name: 'New Experiment',
                    description: 'Test description',
                }
                const mockExperiment = {
                    id: 'exp-123',
                    ...createData,
                    status: 'created',
                    tags: [],
                    metadata: {},
                    created_by: 'user-1',
                    created_at: '2024-01-01T00:00:00Z',
                    updated_at: '2024-01-01T00:00:00Z',
                }
                mockAxiosInstance.post.mockResolvedValueOnce({ data: mockExperiment })

                const result = await experimentsApi.create(createData)

                expect(mockAxiosInstance.post).toHaveBeenCalledWith(
                    '/api/v1/experiments',
                    createData
                )
                expect(result).toEqual(mockExperiment)
            })
        })

        describe('update', () => {
            it('calls PATCH /api/v1/experiments/:id with data', async () => {
                const updateData = {
                    name: 'Updated Experiment',
                    status: 'running',
                }
                const mockExperiment = {
                    id: 'exp-123',
                    project_id: 'project-123',
                    name: 'Updated Experiment',
                    status: 'running',
                    tags: [],
                    metadata: {},
                    created_by: 'user-1',
                    created_at: '2024-01-01T00:00:00Z',
                    updated_at: '2024-01-02T00:00:00Z',
                }
                mockAxiosInstance.patch.mockResolvedValueOnce({ data: mockExperiment })

                const result = await experimentsApi.update('exp-123', updateData)

                expect(mockAxiosInstance.patch).toHaveBeenCalledWith(
                    '/api/v1/experiments/exp-123',
                    updateData
                )
                expect(result).toEqual(mockExperiment)
            })
        })

        describe('delete', () => {
            it('calls DELETE /api/v1/experiments/:id', async () => {
                mockAxiosInstance.delete.mockResolvedValueOnce({ data: {} })

                await experimentsApi.delete('exp-123')

                expect(mockAxiosInstance.delete).toHaveBeenCalledWith('/api/v1/experiments/exp-123')
            })
        })

        describe('search', () => {
            it('calls GET /api/v1/experiments/search with params', async () => {
                const mockResponse = {
                    data: {
                        experiments: [],
                        total: 0,
                        page: 1,
                        page_size: 20,
                    },
                }
                mockAxiosInstance.get.mockResolvedValueOnce(mockResponse)

                const result = await experimentsApi.search({
                    q: 'test query',
                    project_id: 'project-123',
                    page: 1,
                    page_size: 20,
                })

                expect(mockAxiosInstance.get).toHaveBeenCalledWith('/api/v1/experiments/search', {
                    params: {
                        q: 'test query',
                        project_id: 'project-123',
                        page: 1,
                        page_size: 20,
                    },
                })
                expect(result).toEqual(mockResponse.data)
            })
        })
    })

    describe('runsApi', () => {
        describe('list', () => {
            it('calls GET /api/v1/experiments/:id/runs with params', async () => {
                const mockResponse = {
                    data: {
                        runs: [],
                        total: 0,
                        page: 1,
                        page_size: 20,
                    },
                }
                mockAxiosInstance.get.mockResolvedValueOnce(mockResponse)

                const result = await runsApi.list('exp-123', {
                    status: 'running',
                    page: 1,
                    page_size: 20,
                })

                expect(mockAxiosInstance.get).toHaveBeenCalledWith(
                    '/api/v1/experiments/exp-123/runs',
                    {
                        params: {
                            status: 'running',
                            page: 1,
                            page_size: 20,
                        },
                    }
                )
                expect(result).toEqual(mockResponse.data)
            })

            it('calls GET /api/v1/experiments/:id/runs without params', async () => {
                const mockResponse = {
                    data: {
                        runs: [],
                        total: 0,
                        page: 1,
                        page_size: 20,
                    },
                }
                mockAxiosInstance.get.mockResolvedValueOnce(mockResponse)

                const result = await runsApi.list('exp-123')

                expect(mockAxiosInstance.get).toHaveBeenCalledWith(
                    '/api/v1/experiments/exp-123/runs',
                    {
                        params: undefined,
                    }
                )
                expect(result).toEqual(mockResponse.data)
            })
        })

        describe('get', () => {
            it('calls GET /api/v1/runs/:id', async () => {
                const mockRun = {
                    id: 'run-123',
                    experiment_id: 'exp-123',
                    name: 'Test Run',
                    parameters: {},
                    status: 'created',
                    metadata: {},
                    created_at: '2024-01-01T00:00:00Z',
                    updated_at: '2024-01-01T00:00:00Z',
                }
                mockAxiosInstance.get.mockResolvedValueOnce({ data: mockRun })

                const result = await runsApi.get('run-123')

                expect(mockAxiosInstance.get).toHaveBeenCalledWith('/api/v1/runs/run-123')
                expect(result).toEqual(mockRun)
            })
        })

        describe('create', () => {
            it('calls POST /api/v1/experiments/:id/runs with data', async () => {
                const createData = {
                    name: 'New Run',
                    parameters: { param1: 'value1' },
                }
                const mockRun = {
                    id: 'run-123',
                    experiment_id: 'exp-123',
                    ...createData,
                    status: 'created',
                    metadata: {},
                    created_at: '2024-01-01T00:00:00Z',
                    updated_at: '2024-01-01T00:00:00Z',
                }
                mockAxiosInstance.post.mockResolvedValueOnce({ data: mockRun })

                const result = await runsApi.create('exp-123', createData)

                expect(mockAxiosInstance.post).toHaveBeenCalledWith(
                    '/api/v1/experiments/exp-123/runs',
                    createData
                )
                expect(result).toEqual(mockRun)
            })
        })

        describe('update', () => {
            it('calls PATCH /api/v1/runs/:id with data', async () => {
                const updateData = {
                    name: 'Updated Run',
                    status: 'running',
                }
                const mockRun = {
                    id: 'run-123',
                    experiment_id: 'exp-123',
                    name: 'Updated Run',
                    parameters: {},
                    status: 'running',
                    metadata: {},
                    created_at: '2024-01-01T00:00:00Z',
                    updated_at: '2024-01-02T00:00:00Z',
                }
                mockAxiosInstance.patch.mockResolvedValueOnce({ data: mockRun })

                const result = await runsApi.update('run-123', updateData)

                expect(mockAxiosInstance.patch).toHaveBeenCalledWith('/api/v1/runs/run-123', updateData)
                expect(result).toEqual(mockRun)
            })
        })

        describe('complete', () => {
            it('calls PATCH /api/v1/runs/:id with status completed', async () => {
                const mockRun = {
                    id: 'run-123',
                    experiment_id: 'exp-123',
                    name: 'Test Run',
                    parameters: {},
                    status: 'completed',
                    metadata: {},
                    created_at: '2024-01-01T00:00:00Z',
                    updated_at: '2024-01-02T00:00:00Z',
                }
                mockAxiosInstance.patch.mockResolvedValueOnce({ data: mockRun })

                const result = await runsApi.complete('run-123')

                expect(mockAxiosInstance.patch).toHaveBeenCalledWith('/api/v1/runs/run-123', {
                    status: 'completed',
                })
                expect(result).toEqual(mockRun)
            })
        })

        describe('fail', () => {
            it('calls PATCH /api/v1/runs/:id with status failed and reason', async () => {
                const mockRun = {
                    id: 'run-123',
                    experiment_id: 'exp-123',
                    name: 'Test Run',
                    parameters: {},
                    status: 'failed',
                    metadata: {},
                    created_at: '2024-01-01T00:00:00Z',
                    updated_at: '2024-01-02T00:00:00Z',
                }
                mockAxiosInstance.patch.mockResolvedValueOnce({ data: mockRun })

                const result = await runsApi.fail('run-123', 'Test error')

                expect(mockAxiosInstance.patch).toHaveBeenCalledWith('/api/v1/runs/run-123', {
                    status: 'failed',
                    reason: 'Test error',
                })
                expect(result).toEqual(mockRun)
            })

            it('calls PATCH /api/v1/runs/:id with status failed without reason', async () => {
                const mockRun = {
                    id: 'run-123',
                    experiment_id: 'exp-123',
                    name: 'Test Run',
                    parameters: {},
                    status: 'failed',
                    metadata: {},
                    created_at: '2024-01-01T00:00:00Z',
                    updated_at: '2024-01-02T00:00:00Z',
                }
                mockAxiosInstance.patch.mockResolvedValueOnce({ data: mockRun })

                const result = await runsApi.fail('run-123')

                expect(mockAxiosInstance.patch).toHaveBeenCalledWith('/api/v1/runs/run-123', {
                    status: 'failed',
                    reason: undefined,
                })
                expect(result).toEqual(mockRun)
            })
        })
    })

    describe('interceptor', () => {
        it('sets up response interceptor', () => {
            // Interceptor устанавливается при импорте модуля client.ts
            // Проверяем, что interceptor.use был вызван при импорте
            // initialInterceptorCallCount сохраняется ДО первого beforeEach
            expect(initialInterceptorCallCount).toBeGreaterThan(0)
            // Также проверяем, что метод use существует и является мок-функцией
            expect(mockAxiosInstance.interceptors.response.use).toBeDefined()
            expect(vi.isMockFunction(mockAxiosInstance.interceptors.response.use)).toBe(true)
        })
    })
})

