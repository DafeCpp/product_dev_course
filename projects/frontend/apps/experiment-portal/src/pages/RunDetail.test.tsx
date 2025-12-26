import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import RunDetail from './RunDetail'
import { runsApi, experimentsApi, captureSessionsApi } from '../api/client'

// Мокаем API клиенты
vi.mock('../api/client', () => ({
    runsApi: {
        get: vi.fn(),
        complete: vi.fn(),
        fail: vi.fn(),
    },
    experimentsApi: {
        get: vi.fn(),
    },
    captureSessionsApi: {
        list: vi.fn(),
        create: vi.fn(),
        stop: vi.fn(),
        delete: vi.fn(),
    },
}))

// Мокаем useParams
vi.mock('react-router-dom', async () => {
    const actual = await vi.importActual('react-router-dom')
    return {
        ...actual,
        useParams: () => ({ id: 'run-123' }),
    }
})

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
    params: { param1: 'value1', param2: 'value2' },
    status: 'running' as const,
    started_at: '2024-01-01T10:00:00Z',
    finished_at: undefined,
    duration_seconds: 3600,
    notes: 'Test notes',
    metadata: { key: 'value' },
    created_at: '2024-01-01T00:00:00Z',
    updated_at: '2024-01-01T12:00:00Z',
}

const mockExperiment = {
    id: 'exp-123',
    project_id: 'project-123',
    name: 'Test Experiment',
    description: 'Test description',
    experiment_type: 'test',
    owner_id: 'user-123',
    status: 'running' as const,
    tags: [],
    metadata: {},
    created_at: '2024-01-01T00:00:00Z',
    updated_at: '2024-01-01T00:00:00Z',
}

const mockCaptureSession = {
    id: 'session-123',
    run_id: 'run-123',
    project_id: 'project-123',
    ordinal_number: 1,
    started_at: '2024-01-01T10:00:00Z',
    stopped_at: null,
    status: 'running' as const,
    initiated_by: 'user-123',
    notes: 'Test session',
    archived: false,
    created_at: '2024-01-01T10:00:00Z',
    updated_at: '2024-01-01T10:00:00Z',
}

describe('RunDetail', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        // Мокаем window.prompt и window.confirm
        window.prompt = vi.fn(() => 'Test reason')
        window.confirm = vi.fn(() => true)

        // Мокаем experimentsApi и captureSessionsApi по умолчанию
        vi.mocked(experimentsApi.get).mockResolvedValue(mockExperiment)
        vi.mocked(captureSessionsApi.list).mockResolvedValue({
            capture_sessions: [],
            total: 0,
        })
    })

    it('renders loading state', () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockReturnValueOnce(
            new Promise(() => {}) // Never resolves
        )

        render(<RunDetail />, { wrapper: createWrapper() })
        expect(screen.getByText('Загрузка...')).toBeInTheDocument()
    })

    it('renders error state', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockRejectedValueOnce(new Error('Not found'))

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Запуск не найден')).toBeInTheDocument()
        })
    })

    it('renders run details', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce(mockRun)

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Run')).toBeInTheDocument()
            expect(screen.getByText('run-123')).toBeInTheDocument()
            expect(screen.getByText('exp-123')).toBeInTheDocument()
        })
    })

    it('displays status badge', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce(mockRun)

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            const badges = screen.getAllByText('Выполняется')
            const badge = badges.find(el => el.classList.contains('badge'))
            expect(badge).toBeInTheDocument()
        })
    })

    it('displays duration correctly', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce(mockRun)

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/1ч 0м 0с/)).toBeInTheDocument()
        })
    })

    it('displays duration in minutes when less than hour', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce({
            ...mockRun,
            duration_seconds: 3660, // 1 hour 1 minute
        })

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/1ч 1м 0с/)).toBeInTheDocument()
        })
    })

    it('displays duration in seconds when less than minute', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce({
            ...mockRun,
            duration_seconds: 45,
        })

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/45с/)).toBeInTheDocument()
        })
    })

    it('does not display duration when not available', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce({
            ...mockRun,
            duration_seconds: undefined,
        })

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Run')).toBeInTheDocument()
            // Блок с длительностью не должен отображаться, если duration_seconds undefined
            expect(screen.queryByText('Длительность:')).not.toBeInTheDocument()
        })
    })

    it('displays notes', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce(mockRun)

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test notes')).toBeInTheDocument()
        })
    })

    it('displays parameters', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce(mockRun)

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/param1/i)).toBeInTheDocument()
            expect(screen.getByText(/value1/i)).toBeInTheDocument()
        })
    })

    it('displays metadata', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce(mockRun)

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/key.*value/i)).toBeInTheDocument()
        })
    })

    it('shows complete button for running status', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce(mockRun)

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Завершить')).toBeInTheDocument()
        })
    })

    it('completes run on complete button click', async () => {
        const user = userEvent.setup()
        const mockGet = vi.mocked(runsApi.get)
        const mockComplete = vi.mocked(runsApi.complete)

        mockGet.mockResolvedValueOnce(mockRun)
        mockComplete.mockResolvedValueOnce({
            ...mockRun,
            status: 'succeeded' as const,
        })

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Run')).toBeInTheDocument()
        })

        const completeButton = screen.getByText('Завершить')
        await user.click(completeButton)

        await waitFor(() => {
            expect(mockComplete).toHaveBeenCalledWith('run-123')
        })
    })

    it('fails run on fail button click', async () => {
        const user = userEvent.setup()
        const mockGet = vi.mocked(runsApi.get)
        const mockFail = vi.mocked(runsApi.fail)

        mockGet.mockResolvedValueOnce(mockRun)
        mockFail.mockResolvedValueOnce({
            ...mockRun,
            status: 'failed' as const,
        })

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Run')).toBeInTheDocument()
        })

        const failButton = screen.getByText('Пометить как ошибка')
        await user.click(failButton)

        await waitFor(() => {
            expect(window.prompt).toHaveBeenCalledWith('Причина ошибки:')
            expect(mockFail).toHaveBeenCalledWith('run-123', 'Test reason')
        })
    })

    it('does not fail run if prompt is cancelled', async () => {
        const user = userEvent.setup()
        const mockGet = vi.mocked(runsApi.get)
        const mockFail = vi.mocked(runsApi.fail)

        window.prompt = vi.fn(() => null)

        mockGet.mockResolvedValueOnce(mockRun)

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Run')).toBeInTheDocument()
        })

        const failButton = screen.getByText('Пометить как ошибка')
        await user.click(failButton)

        await waitFor(() => {
            expect(window.prompt).toHaveBeenCalled()
            expect(mockFail).not.toHaveBeenCalled()
        })
    })

    it('does not show action buttons for succeeded status', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce({
            ...mockRun,
            status: 'succeeded' as const,
        })

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Run')).toBeInTheDocument()
            expect(screen.queryByText('Завершить')).not.toBeInTheDocument()
            expect(screen.queryByText('Пометить как ошибка')).not.toBeInTheDocument()
        })
    })

    it('displays link to experiment', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce(mockRun)

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            const link = screen.getByText(/вернуться к эксперименту/i)
            expect(link).toBeInTheDocument()
            expect(link.closest('a')).toHaveAttribute('href', '/experiments/exp-123')
        })
    })

    it('handles run without notes', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce({
            ...mockRun,
            notes: undefined,
        })

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Run')).toBeInTheDocument()
            expect(screen.queryByText('Test notes')).not.toBeInTheDocument()
        })
    })

    it('handles run without metadata', async () => {
        const mockGet = vi.mocked(runsApi.get)
        mockGet.mockResolvedValueOnce({
            ...mockRun,
            metadata: {},
        })

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Run')).toBeInTheDocument()
        })
    })

    it('disables buttons during mutation', async () => {
        const user = userEvent.setup()
        const mockGet = vi.mocked(runsApi.get)
        const mockComplete = vi.mocked(runsApi.complete)

        let resolveComplete: (value: any) => void
        const completePromise = new Promise((resolve) => {
            resolveComplete = resolve
        })
        mockComplete.mockReturnValueOnce(completePromise as any)

        mockGet.mockResolvedValueOnce(mockRun)

        render(<RunDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Run')).toBeInTheDocument()
        })

        const completeButton = screen.getByText('Завершить')
        await user.click(completeButton)

        await waitFor(() => {
            expect(completeButton).toBeDisabled()
        })

        resolveComplete!({
            ...mockRun,
            status: 'succeeded' as const,
        })
    })

    describe('Capture Sessions', () => {
        it('displays capture sessions section', async () => {
            const mockGet = vi.mocked(runsApi.get)
            mockGet.mockResolvedValueOnce(mockRun)

            render(<RunDetail />, { wrapper: createWrapper() })

            await waitFor(() => {
                expect(screen.getByText('Сессии отсчёта')).toBeInTheDocument()
            })
        })

        it('shows start button when run is running and no active session', async () => {
            const mockGet = vi.mocked(runsApi.get)
            mockGet.mockResolvedValueOnce(mockRun)

            render(<RunDetail />, { wrapper: createWrapper() })

            await waitFor(() => {
                expect(screen.getByText('Старт отсчёта')).toBeInTheDocument()
            })
        })

        it('creates capture session on start button click', async () => {
            const user = userEvent.setup()
            const mockGet = vi.mocked(runsApi.get)
            const mockCreate = vi.mocked(captureSessionsApi.create)

            window.prompt = vi.fn(() => 'Session notes')

            mockGet.mockResolvedValueOnce(mockRun)
            mockCreate.mockResolvedValueOnce(mockCaptureSession)

            render(<RunDetail />, { wrapper: createWrapper() })

            await waitFor(() => {
                expect(screen.getByText('Старт отсчёта')).toBeInTheDocument()
            })

            const startButton = screen.getByText('Старт отсчёта')
            await user.click(startButton)

            await waitFor(() => {
                expect(window.prompt).toHaveBeenCalledWith('Заметки (опционально):')
                expect(mockCreate).toHaveBeenCalled()
            })
        })

        it('shows stop button when active session exists', async () => {
            const mockGet = vi.mocked(runsApi.get)
            const mockList = vi.mocked(captureSessionsApi.list)

            mockGet.mockResolvedValueOnce(mockRun)
            mockList.mockResolvedValueOnce({
                capture_sessions: [mockCaptureSession],
                total: 1,
            })

            render(<RunDetail />, { wrapper: createWrapper() })

            await waitFor(() => {
                expect(screen.getByText('Стоп отсчёта')).toBeInTheDocument()
            })
        })

        it('stops capture session on stop button click', async () => {
            const user = userEvent.setup()
            const mockGet = vi.mocked(runsApi.get)
            const mockList = vi.mocked(captureSessionsApi.list)
            const mockStop = vi.mocked(captureSessionsApi.stop)

            mockGet.mockResolvedValueOnce(mockRun)
            mockList.mockResolvedValueOnce({
                capture_sessions: [mockCaptureSession],
                total: 1,
            })
            mockStop.mockResolvedValueOnce({
                ...mockCaptureSession,
                status: 'succeeded' as const,
                stopped_at: '2024-01-01T11:00:00Z',
            })

            render(<RunDetail />, { wrapper: createWrapper() })

            await waitFor(() => {
                expect(screen.getByText('Стоп отсчёта')).toBeInTheDocument()
            })

            const stopButton = screen.getByText('Стоп отсчёта')
            await user.click(stopButton)

            await waitFor(() => {
                expect(window.confirm).toHaveBeenCalledWith('Остановить отсчёт?')
                expect(mockStop).toHaveBeenCalledWith('run-123', 'session-123')
            })
        })

        it('displays active session information', async () => {
            const mockGet = vi.mocked(runsApi.get)
            const mockList = vi.mocked(captureSessionsApi.list)

            mockGet.mockResolvedValueOnce(mockRun)
            mockList.mockResolvedValueOnce({
                capture_sessions: [mockCaptureSession],
                total: 1,
            })

            render(<RunDetail />, { wrapper: createWrapper() })

            await waitFor(() => {
                expect(screen.getByText(/Активная сессия #1/i)).toBeInTheDocument()
            })

            // Проверяем, что статус capture session отображается в секции сессий
            const sessionCard = screen.getByText(/Активная сессия #1/i).closest('.session-card')
            expect(sessionCard).toBeInTheDocument()

            // Находим badge внутри session-card
            const sessionBadges = sessionCard?.querySelectorAll('.badge')
            expect(sessionBadges?.length).toBeGreaterThan(0)

            // Проверяем, что есть badge с текстом "Выполняется" в session-card
            const sessionBadge = Array.from(sessionBadges || []).find(badge =>
                badge.textContent === 'Выполняется'
            )
            expect(sessionBadge).toBeInTheDocument()
        })

        it('displays list of completed sessions', async () => {
            const mockGet = vi.mocked(runsApi.get)
            const mockList = vi.mocked(captureSessionsApi.list)

            const completedSession = {
                ...mockCaptureSession,
                id: 'session-456',
                ordinal_number: 2,
                status: 'succeeded' as const,
                stopped_at: '2024-01-01T11:00:00Z',
            }

            mockGet.mockResolvedValueOnce(mockRun)
            mockList.mockResolvedValueOnce({
                capture_sessions: [completedSession],
                total: 1,
            })

            render(<RunDetail />, { wrapper: createWrapper() })

            await waitFor(() => {
                expect(screen.getByText(/Сессия #2/i)).toBeInTheDocument()
                expect(screen.getByText('Успешно')).toBeInTheDocument()
            })
        })

        it('shows empty state when no sessions', async () => {
            const mockGet = vi.mocked(runsApi.get)
            mockGet.mockResolvedValueOnce(mockRun)

            render(<RunDetail />, { wrapper: createWrapper() })

            await waitFor(() => {
                expect(screen.getByText('Сессии отсчёта отсутствуют')).toBeInTheDocument()
            })
        })
    })
})

