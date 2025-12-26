import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ExperimentsList from './ExperimentsList'
import { experimentsApi } from '../api/client'

// Мокаем experimentsApi
vi.mock('../api/client', () => ({
    experimentsApi: {
        list: vi.fn(),
        search: vi.fn(),
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

const mockExperiment = {
    id: 'exp-1',
    project_id: 'project-1',
    name: 'Test Experiment',
    description: 'Test description',
    experiment_type: 'aerodynamics',
    status: 'draft' as const,
    tags: ['test', 'aerodynamics'],
    metadata: {},
    owner_id: 'user-1',
    created_at: '2024-01-01T00:00:00Z',
    updated_at: '2024-01-01T00:00:00Z',
}

describe('ExperimentsList', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('renders loading state', () => {
        const mockList = vi.mocked(experimentsApi.list)
        mockList.mockReturnValueOnce(
            new Promise(() => {}) // Never resolves
        )

        render(<ExperimentsList />, { wrapper: createWrapper() })
        expect(screen.getByText('Загрузка...')).toBeInTheDocument()
    })

    it('renders error state', async () => {
        const mockList = vi.mocked(experimentsApi.list)
        mockList.mockRejectedValueOnce(new Error('Network error'))

        render(<ExperimentsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Ошибка загрузки экспериментов')).toBeInTheDocument()
        })
    })

    it('renders experiments list', async () => {
        const mockList = vi.mocked(experimentsApi.list)
        mockList.mockResolvedValueOnce({
            experiments: [mockExperiment],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Experiment')).toBeInTheDocument()
            expect(screen.getByText('Test description')).toBeInTheDocument()
        })
    })

    it('renders empty state when no experiments', async () => {
        const mockList = vi.mocked(experimentsApi.list)
        mockList.mockResolvedValueOnce({
            experiments: [],
            total: 0,
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Эксперименты не найдены')).toBeInTheDocument()
        })
    })

    it('filters by search query', async () => {
        const user = userEvent.setup()
        const mockSearch = vi.mocked(experimentsApi.search)
        const mockList = vi.mocked(experimentsApi.list)

        mockList.mockResolvedValueOnce({
            experiments: [],
            total: 0,
            page: 1,
            page_size: 20,
        })

        mockSearch.mockResolvedValueOnce({
            experiments: [mockExperiment],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.queryByText('Загрузка...')).not.toBeInTheDocument()
        })

        const searchInput = screen.getByPlaceholderText('Название, описание...')
        // Используем paste для ввода всего текста сразу, чтобы избежать множественных вызовов
        await user.clear(searchInput)
        await user.paste('test')

        // Ждем, пока будет вызов с полным текстом 'test'
        await waitFor(() => {
            const calls = mockSearch.mock.calls
            expect(calls.length).toBeGreaterThan(0)
            // Ищем вызов с полным текстом 'test'
            const callWithFullText = calls.find(call => call[0]?.q === 'test')
            expect(callWithFullText).toBeDefined()
            expect(callWithFullText![0]).toEqual({
                q: 'test',
                project_id: undefined,
                page: 1,
                page_size: 20,
            })
        }, { timeout: 3000 })
    })

    it('filters by project ID', async () => {
        const user = userEvent.setup()
        const mockList = vi.mocked(experimentsApi.list)

        mockList.mockResolvedValueOnce({
            experiments: [],
            total: 0,
            page: 1,
            page_size: 20,
        })

        mockList.mockResolvedValueOnce({
            experiments: [mockExperiment],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.queryByText('Загрузка...')).not.toBeInTheDocument()
        })

        const projectInput = screen.getByPlaceholderText('UUID проекта')
        // Используем paste для ввода всего текста сразу, чтобы избежать множественных вызовов
        await user.clear(projectInput)
        await user.paste('project-123')

        // Ждем, пока будет вызов с полным текстом 'project-123'
        await waitFor(() => {
            const calls = mockList.mock.calls
            expect(calls.length).toBeGreaterThan(0)
            // Ищем вызов с полным текстом 'project-123'
            const callWithFullText = calls.find(call => call[0]?.project_id === 'project-123')
            expect(callWithFullText).toBeDefined()
            expect(callWithFullText![0]).toEqual({
                project_id: 'project-123',
                status: undefined,
                page: 1,
                page_size: 20,
            })
        }, { timeout: 3000 })
    })

    it('filters by status', async () => {
        const user = userEvent.setup()
        const mockList = vi.mocked(experimentsApi.list)

        mockList.mockResolvedValueOnce({
            experiments: [mockExperiment],
            total: 1,
            page: 1,
            page_size: 20,
        })

        mockList.mockResolvedValueOnce({
            experiments: [mockExperiment],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.queryByText('Загрузка...')).not.toBeInTheDocument()
        })

        // Ищем select по его родительскому label
        const statusLabel = screen.getByText('Статус')
        const statusSelect = statusLabel.parentElement?.querySelector('select')
        expect(statusSelect).toBeInTheDocument()
        await user.selectOptions(statusSelect!, 'running')

        await waitFor(() => {
            expect(mockList).toHaveBeenCalledWith({
                project_id: undefined,
                status: 'running',
                page: 1,
                page_size: 20,
            })
        })
    })

    it('displays status badges correctly', async () => {
        const mockList = vi.mocked(experimentsApi.list)
        mockList.mockResolvedValueOnce({
            experiments: [
                { ...mockExperiment, status: 'draft' },
                { ...mockExperiment, id: 'exp-2', status: 'running' },
                { ...mockExperiment, id: 'exp-3', status: 'succeeded' },
                { ...mockExperiment, id: 'exp-4', status: 'failed' },
            ],
            total: 4,
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            // Ищем badges по классу, чтобы избежать конфликта с option в select
            const draftBadge = screen.getByText('Черновик', { selector: '.badge' })
            const runningBadge = screen.getByText('Выполняется', { selector: '.badge' })
            const succeededBadge = screen.getByText('Успешно', { selector: '.badge' })
            const failedBadge = screen.getByText('Ошибка', { selector: '.badge' })
            expect(draftBadge).toBeInTheDocument()
            expect(runningBadge).toBeInTheDocument()
            expect(succeededBadge).toBeInTheDocument()
            expect(failedBadge).toBeInTheDocument()
        })
    })

    it('handles pagination', async () => {
        const user = userEvent.setup()
        const mockList = vi.mocked(experimentsApi.list)

        mockList.mockResolvedValueOnce({
            experiments: Array(20).fill(mockExperiment),
            total: 45,
            page: 1,
            page_size: 20,
        })

        mockList.mockResolvedValueOnce({
            experiments: Array(20).fill(mockExperiment),
            total: 45,
            page: 2,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.queryByText('Загрузка...')).not.toBeInTheDocument()
        })

        // Ждем появления пагинации
        await waitFor(() => {
            expect(screen.getByText('Вперед')).toBeInTheDocument()
        })

        const nextButton = screen.getByText('Вперед')
        await user.click(nextButton)

        await waitFor(() => {
            expect(mockList).toHaveBeenCalledWith(
                expect.objectContaining({
                    page: 2,
                })
            )
        })
    })

    it('disables pagination buttons correctly', async () => {
        const mockList = vi.mocked(experimentsApi.list)
        // Нужно total > pageSize, чтобы пагинация отображалась, но меньше 2 страниц
        mockList.mockResolvedValueOnce({
            experiments: Array(20).fill(mockExperiment),
            total: 20, // Ровно одна страница
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.queryByText('Загрузка...')).not.toBeInTheDocument()
        })

        // Пагинация не должна отображаться, если total <= pageSize
        await waitFor(() => {
            expect(screen.queryByText('Назад')).not.toBeInTheDocument()
            expect(screen.queryByText('Вперед')).not.toBeInTheDocument()
        })
    })

    it('displays tags', async () => {
        const mockList = vi.mocked(experimentsApi.list)
        mockList.mockResolvedValueOnce({
            experiments: [mockExperiment],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            const tags = screen.getAllByText('test')
            expect(tags.length).toBeGreaterThan(0)
            const aerodynamicsTags = screen.getAllByText('aerodynamics')
            expect(aerodynamicsTags.length).toBeGreaterThan(0)
        })
    })

    it('has link to create experiment', async () => {
        const mockList = vi.mocked(experimentsApi.list)
        mockList.mockResolvedValueOnce({
            experiments: [],
            total: 0,
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            const createLink = screen.getByText('Создать эксперимент')
            expect(createLink).toBeInTheDocument()
            expect(createLink.closest('a')).toHaveAttribute('href', '/experiments/new')
        })
    })
})

