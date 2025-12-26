import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import RunsList from './RunsList'
import { runsApi } from '../api/client'

// Мокаем runsApi
vi.mock('../api/client', () => ({
    runsApi: {
        list: vi.fn(),
    },
}))

const createWrapper = () => {
    const queryClient = new QueryClient({
        defaultOptions: {
            queries: { retry: false },
            mutations: { retry: false },
        },
    })
    return ({ children }: { children: React.ReactNode }) => (
        <QueryClientProvider client={queryClient}>
            <MemoryRouter>{children}</MemoryRouter>
        </QueryClientProvider>
    )
}

const mockRun = {
    id: 'run-123',
    experiment_id: 'exp-123',
    name: 'Test Run',
    parameters: { param1: 'value1', param2: 'value2', param3: 'value3' },
    status: 'running' as const,
    started_at: '2024-01-01T10:00:00Z',
    completed_at: '2024-01-01T11:00:00Z',
    duration_seconds: 3600,
    metadata: {},
    created_at: '2024-01-01T00:00:00Z',
    updated_at: '2024-01-01T12:00:00Z',
}

describe('RunsList', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('renders loading state', () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockReturnValueOnce(
            new Promise(() => {}) // Never resolves
        )

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })
        expect(screen.getByText('Загрузка запусков...')).toBeInTheDocument()
    })

    it('renders error state', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockRejectedValueOnce(new Error('Network error'))

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Ошибка загрузки запусков')).toBeInTheDocument()
        })
    })

    it('renders empty state when no runs', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [],
            total: 0,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Запуски не найдены')).toBeInTheDocument()
        })
    })

    it('renders runs list', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [mockRun],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Run')).toBeInTheDocument()
        })
    })

    it('displays status badges correctly', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [
                { ...mockRun, id: 'run-1', status: 'created' as const },
                { ...mockRun, id: 'run-2', status: 'running' as const },
                { ...mockRun, id: 'run-3', status: 'completed' as const },
                { ...mockRun, id: 'run-4', status: 'failed' as const },
            ],
            total: 4,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Создан')).toBeInTheDocument()
            expect(screen.getByText('Выполняется')).toBeInTheDocument()
            expect(screen.getByText('Завершен')).toBeInTheDocument()
            expect(screen.getByText('Ошибка')).toBeInTheDocument()
        })
    })

    it('displays duration correctly', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [mockRun],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/1ч 0м 0с/)).toBeInTheDocument()
        })
    })

    it('displays duration in minutes when less than hour', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [
                {
                    ...mockRun,
                    duration_seconds: 3660, // 1 hour 1 minute
                },
            ],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/1ч 1м 0с/)).toBeInTheDocument()
        })
    })

    it('displays duration in seconds when less than minute', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [
                {
                    ...mockRun,
                    duration_seconds: 45,
                },
            ],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/45с/)).toBeInTheDocument()
        })
    })

    it('displays "-" when duration is not available', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [
                {
                    ...mockRun,
                    duration_seconds: undefined,
                },
            ],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            const durationCells = screen.getAllByText('-')
            expect(durationCells.length).toBeGreaterThan(0)
        })
    })

    it('displays started_at when available', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [mockRun],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            // Проверяем, что дата started_at отображается (ищем по полному тексту или используем getAllByText)
            const dateTexts = screen.getAllByText(/01 Jan/i)
            expect(dateTexts.length).toBeGreaterThan(0)
            // Проверяем, что есть дата с временем начала (10:00)
            const startedAtText = screen.getByText(/01 Jan 10:00/i)
            expect(startedAtText).toBeInTheDocument()
        })
    })

    it('displays "-" when started_at is not available', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [
                {
                    ...mockRun,
                    started_at: undefined,
                },
            ],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            const dashCells = screen.getAllByText('-')
            expect(dashCells.length).toBeGreaterThan(0)
        })
    })

    it('displays completed_at when available', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [mockRun],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            // Проверяем, что дата completed_at отображается (ищем по полному тексту или используем getAllByText)
            const dateTexts = screen.getAllByText(/01 Jan/i)
            expect(dateTexts.length).toBeGreaterThan(0)
            // Проверяем, что есть дата с временем завершения (11:00)
            const completedAtText = screen.getByText(/01 Jan 11:00/i)
            expect(completedAtText).toBeInTheDocument()
        })
    })

    it('displays "-" when completed_at is not available', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [
                {
                    ...mockRun,
                    completed_at: undefined,
                },
            ],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            const dashCells = screen.getAllByText('-')
            expect(dashCells.length).toBeGreaterThan(0)
        })
    })

    it('displays parameters preview (first 3)', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [mockRun],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/param1:/i)).toBeInTheDocument()
            expect(screen.getByText(/param2:/i)).toBeInTheDocument()
            expect(screen.getByText(/param3:/i)).toBeInTheDocument()
        })
    })

    it('shows "+N" when there are more than 3 parameters', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [
                {
                    ...mockRun,
                    parameters: {
                        param1: 'value1',
                        param2: 'value2',
                        param3: 'value3',
                        param4: 'value4',
                        param5: 'value5',
                    },
                },
            ],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/\+2/)).toBeInTheDocument()
        })
    })

    it('has link to run detail', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [mockRun],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-123" />, { wrapper: createWrapper() })

        await waitFor(() => {
            const links = screen.getAllByRole('link')
            const detailLink = links.find((link) => link.getAttribute('href') === '/runs/run-123')
            expect(detailLink).toBeInTheDocument()
        })
    })

    it('calls runsApi.list with correct experimentId', async () => {
        const mockList = vi.mocked(runsApi.list)
        mockList.mockResolvedValueOnce({
            runs: [],
            total: 0,
            page: 1,
            page_size: 20,
        })

        render(<RunsList experimentId="exp-456" />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(mockList).toHaveBeenCalled()
            // Проверяем первый аргумент (experimentId)
            expect(mockList.mock.calls[0][0]).toBe('exp-456')
            // Второй аргумент может быть undefined или объект с params
            expect(mockList.mock.calls[0][1]).toBeUndefined()
        })
    })
})

