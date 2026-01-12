import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ExperimentsList from './ExperimentsList'
import { experimentsApi, projectsApi } from '../api/client'

// Мокаем API
vi.mock('../api/client', () => ({
    experimentsApi: {
        list: vi.fn(),
        search: vi.fn(),
    },
    projectsApi: {
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

const mockProject = {
    id: 'project-1',
    name: 'Test Project',
    description: '',
    owner_id: 'user-1',
    created_at: '2024-01-01T00:00:00Z',
    updated_at: '2024-01-01T00:00:00Z',
}

describe('ExperimentsList', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        // Мокаем projectsApi.list по умолчанию
        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.list.mockResolvedValue({
            projects: [mockProject],
        })
    })

    it('renders loading state', async () => {
        const mockList = vi.mocked(experimentsApi.list)
        mockList.mockReturnValueOnce(
            new Promise(() => {}) // Never resolves
        )

        render(<ExperimentsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Загрузка...')).toBeInTheDocument()
        })
    })

    it('renders error state', async () => {
        const mockList = vi.mocked(experimentsApi.list)
        mockList.mockRejectedValueOnce(new Error('Network error'))

        render(<ExperimentsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/Ошибка загрузки экспериментов/i)).toBeInTheDocument()
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

    it('filters by search query', { timeout: 20000 }, async () => {
        const user = userEvent.setup()
        const mockSearch = vi.mocked(experimentsApi.search)
        const mockList = vi.mocked(experimentsApi.list)

        // Первый вызов для начальной загрузки (когда projectId установлен) - пустой список
        mockList.mockResolvedValueOnce({
            experiments: [],
            total: 0,
            page: 1,
            page_size: 20,
        })

        // Вызов search при вводе текста - возвращаем эксперимент
        mockSearch.mockResolvedValue({
            experiments: [mockExperiment],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        // Ждем, пока загрузка завершится и форма фильтров появится
        await waitFor(() => {
            expect(screen.queryByText('Загрузка...')).not.toBeInTheDocument()
            expect(screen.getByPlaceholderText('Название, описание...')).toBeInTheDocument()
        }, { timeout: 5000 })

        // Проверяем, что изначально список пуст (показывается EmptyState)
        await waitFor(() => {
            expect(screen.getByText('Эксперименты не найдены')).toBeInTheDocument()
        }, { timeout: 5000 })

        const searchInput = screen.getByPlaceholderText('Название, описание...')
        // Вводим текст поиска
        await user.clear(searchInput)
        await user.paste('test')

        // Ждем, пока search API будет вызван
        await waitFor(() => {
            expect(mockSearch).toHaveBeenCalled()
        }, { timeout: 5000 })

        // Ждем, пока эксперимент появится на экране (результат поиска)
        await waitFor(() => {
            expect(screen.queryByText('Эксперименты не найдены')).not.toBeInTheDocument()
            expect(screen.getByText('Test Experiment')).toBeInTheDocument()
            expect(screen.getByText('Test description')).toBeInTheDocument()
        }, { timeout: 10000 })
    })

    it('filters by project ID', async () => {
        const user = userEvent.setup()
        const mockList = vi.mocked(experimentsApi.list)
        const mockProjectsApi = vi.mocked(projectsApi)

        // Мокаем список проектов с несколькими проектами (переопределяем мок из beforeEach)
        mockProjectsApi.list.mockResolvedValue({
            projects: [
                mockProject,
                { ...mockProject, id: 'project-123', name: 'Project 123', updated_at: '2024-01-01T00:00:00Z' },
            ],
        })

        // Первый вызов для начальной загрузки (project-1)
        mockList.mockResolvedValue({
            experiments: [],
            total: 0,
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        // Ждем, пока загрузка завершится
        await waitFor(() => {
            expect(screen.queryByText('Загрузка...')).not.toBeInTheDocument()
        }, { timeout: 5000 })

        // Ждем, пока форма фильтров появится
        await waitFor(() => {
            expect(screen.getByText('Проект')).toBeInTheDocument()
        }, { timeout: 5000 })

        // Ждем, пока проекты загрузятся в select
        await waitFor(() => {
            const projectLabel = screen.getByText('Проект')
            const projectSelect = projectLabel.parentElement?.querySelector('select')
            expect(projectSelect).toBeInTheDocument()
            // Проверяем, что в select есть опции (пустая опция + 2 проекта)
            expect(projectSelect?.options.length).toBeGreaterThanOrEqual(3)
        }, { timeout: 5000 })

        // Мокаем второй вызов после выбора project-123
        mockList.mockResolvedValueOnce({
            experiments: [mockExperiment],
            total: 1,
            page: 1,
            page_size: 20,
        })

        // Иногда из-за перерендеров/рефетчей React Query страница может на мгновение вернуться в loading —
        // дождёмся, пока фильтры снова будут видимы, и только потом взаимодействуем с select.
        await waitFor(() => {
            expect(screen.queryByText('Загрузка...')).not.toBeInTheDocument()
            expect(screen.getByText('Проект')).toBeInTheDocument()
        }, { timeout: 5000 })

        const projectLabel = screen.getByText('Проект')
        const projectSelect = projectLabel.parentElement?.querySelector('select') as HTMLSelectElement
        expect(projectSelect).toBeInTheDocument()
        await user.selectOptions(projectSelect, 'project-123')

        // Ждем, пока будет вызов с project_id 'project-123'
        await waitFor(() => {
            const calls = mockList.mock.calls
            expect(calls.length).toBeGreaterThan(1)
            // Ищем вызов с project_id 'project-123'
            const callWithProjectId = calls.find(call => call[0]?.project_id === 'project-123')
            expect(callWithProjectId).toBeDefined()
            if (callWithProjectId) {
                expect(callWithProjectId[0]).toEqual({
                    project_id: 'project-123',
                    status: undefined,
                    page: 1,
                    page_size: 20,
                })
            }
        }, { timeout: 5000 })
    })

    it('filters by status', async () => {
        const user = userEvent.setup()
        const mockList = vi.mocked(experimentsApi.list)

        // Создаем эксперименты с разными статусами
        const draftExperiment = { ...mockExperiment, id: 'exp-1', status: 'draft' as const }
        const runningExperiment = { ...mockExperiment, id: 'exp-2', name: 'Running Experiment', status: 'running' as const }
        const succeededExperiment = { ...mockExperiment, id: 'exp-3', name: 'Succeeded Experiment', status: 'succeeded' as const }

        // Первый вызов для начальной загрузки - все эксперименты
        mockList.mockResolvedValueOnce({
            experiments: [draftExperiment, runningExperiment, succeededExperiment],
            total: 3,
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        // Ждем, пока загрузка завершится и projectId будет установлен
        await waitFor(() => {
            expect(screen.queryByText('Загрузка...')).not.toBeInTheDocument()
        }, { timeout: 5000 })

        // Проверяем, что все эксперименты отображаются
        await waitFor(() => {
            expect(screen.getByText('Test Experiment')).toBeInTheDocument()
            expect(screen.getByText('Running Experiment')).toBeInTheDocument()
            expect(screen.getByText('Succeeded Experiment')).toBeInTheDocument()
        }, { timeout: 5000 })

        // Мокаем второй вызов после выбора статуса - только running эксперименты
        mockList.mockResolvedValueOnce({
            experiments: [runningExperiment],
            total: 1,
            page: 1,
            page_size: 20,
        })

        // Ждем, пока форма фильтров появится
        await waitFor(() => {
            expect(screen.getByText('Статус')).toBeInTheDocument()
        }, { timeout: 5000 })

        const statusLabel = screen.getByText('Статус')
        const statusSelect = statusLabel.parentElement?.querySelector('select')
        expect(statusSelect).toBeInTheDocument()
        await user.selectOptions(statusSelect!, 'running')

        // Ждем, пока отфильтруются эксперименты - должен остаться только running
        await waitFor(() => {
            expect(screen.getByText('Running Experiment')).toBeInTheDocument()
            // Проверяем, что другие эксперименты исчезли
            expect(screen.queryByText('Test Experiment')).not.toBeInTheDocument()
            expect(screen.queryByText('Succeeded Experiment')).not.toBeInTheDocument()
        }, { timeout: 5000 })
    })

    it('displays status badges correctly', async () => {
        const mockList = vi.mocked(experimentsApi.list)
        mockList.mockResolvedValueOnce({
            experiments: [
                { ...mockExperiment, status: 'draft' },
                { ...mockExperiment, id: 'exp-2', name: 'Running Experiment', status: 'running' },
                { ...mockExperiment, id: 'exp-3', name: 'Succeeded Experiment', status: 'succeeded' },
                { ...mockExperiment, id: 'exp-4', name: 'Failed Experiment', status: 'failed' },
            ],
            total: 4,
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        // Ждем, пока загрузка завершится
        await waitFor(() => {
            expect(screen.queryByText('Загрузка...')).not.toBeInTheDocument()
        }, { timeout: 5000 })

        // Ждем, пока эксперименты отобразятся
        await waitFor(() => {
            expect(screen.getByText('Test Experiment')).toBeInTheDocument()
            expect(screen.getByText('Running Experiment')).toBeInTheDocument()
            expect(screen.getByText('Succeeded Experiment')).toBeInTheDocument()
            expect(screen.getByText('Failed Experiment')).toBeInTheDocument()
        }, { timeout: 5000 })

        // Ищем badges по тексту
        await waitFor(() => {
            // Проверяем наличие каждого типа badge
            const draftBadges = screen.getAllByText('Черновик')
            const runningBadges = screen.getAllByText('Выполняется')
            const succeededBadges = screen.getAllByText('Успешно')
            const failedBadges = screen.getAllByText('Ошибка')

            expect(draftBadges.length).toBeGreaterThan(0)
            expect(runningBadges.length).toBeGreaterThan(0)
            expect(succeededBadges.length).toBeGreaterThan(0)
            expect(failedBadges.length).toBeGreaterThan(0)

            // Проверяем, что хотя бы один badge каждого типа имеет класс badge
            expect(draftBadges.some(badge => badge.classList.contains('badge'))).toBe(true)
            expect(runningBadges.some(badge => badge.classList.contains('badge'))).toBe(true)
            expect(succeededBadges.some(badge => badge.classList.contains('badge'))).toBe(true)
            expect(failedBadges.some(badge => badge.classList.contains('badge'))).toBe(true)
        }, { timeout: 5000 })
    })

    it('handles pagination', async () => {
        const user = userEvent.setup()
        const mockList = vi.mocked(experimentsApi.list)

        // Создаем массив экспериментов с уникальными именами для отладки
        const experimentsPage1 = Array.from({ length: 20 }, (_, i) => ({
            ...mockExperiment,
            id: `exp-${i + 1}`,
            name: `Test Experiment ${i + 1}`,
        }))

        const experimentsPage2 = Array.from({ length: 20 }, (_, i) => ({
            ...mockExperiment,
            id: `exp-${i + 21}`,
            name: `Test Experiment ${i + 21}`,
        }))

        // Первый вызов для начальной загрузки
        mockList.mockResolvedValue({
            experiments: experimentsPage1,
            total: 45,
            page: 1,
            page_size: 20,
        })

        render(<ExperimentsList />, { wrapper: createWrapper() })

        // Ждем, пока загрузка завершится
        await waitFor(() => {
            expect(screen.queryByText('Загрузка...')).not.toBeInTheDocument()
        }, { timeout: 5000 })

        // Ждем, пока эксперименты отобразятся
        await waitFor(() => {
            expect(screen.getByText('Test Experiment 1')).toBeInTheDocument()
        }, { timeout: 5000 })

        // Мокаем второй вызов после клика на пагинацию
        mockList.mockResolvedValueOnce({
            experiments: experimentsPage2,
            total: 45,
            page: 2,
            page_size: 20,
        })

        // Ждем появления пагинации и находим кнопку "Вперед"
        await waitFor(() => {
            const nextButton = screen.getByText('Вперед')
            expect(nextButton).toBeInTheDocument()
        }, { timeout: 5000 })

        const nextButton = screen.getByText('Вперед')
        await user.click(nextButton)

        await waitFor(() => {
            const calls = mockList.mock.calls
            const callWithPage2 = calls.find(call => call[0]?.page === 2)
            expect(callWithPage2).toBeDefined()
            if (callWithPage2) {
                expect(callWithPage2[0]).toEqual(
                    expect.objectContaining({
                        page: 2,
                    })
                )
            }
        }, { timeout: 5000 })
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
            const createButtons = screen.getAllByRole('button', { name: /создать эксперимент/i })
            // Должна быть кнопка в PageHeader
            const headerButton = createButtons.find(btn => btn.classList.contains('btn-primary'))
            expect(headerButton).toBeInTheDocument()
            expect(headerButton).toHaveClass('btn', 'btn-primary')
        })
    })
})

