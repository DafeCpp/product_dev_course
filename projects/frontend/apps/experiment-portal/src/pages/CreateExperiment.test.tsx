import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor, fireEvent } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import CreateExperimentModal from '../components/CreateExperimentModal'
import { experimentsApi, projectsApi } from '../api/client'

// Мокаем API
vi.mock('../api/client', () => ({
    experimentsApi: {
        create: vi.fn(),
    },
    projectsApi: {
        list: vi.fn(),
    },
}))

// Мокаем useNavigate
const mockNavigate = vi.fn()
vi.mock('react-router-dom', async () => {
    const actual = await vi.importActual('react-router-dom')
    return {
        ...actual,
        useNavigate: () => mockNavigate,
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

describe('CreateExperimentModal', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockNavigate.mockClear()
        window.localStorage.clear()
        // Мокаем projectsApi.list по умолчанию
        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.list.mockResolvedValue({
            projects: [
                {
                    id: 'project-1',
                    name: 'Test Project',
                    description: '',
                    owner_id: 'user-1',
                    created_at: '2024-01-01T00:00:00Z',
                    updated_at: '2024-01-01T00:00:00Z',
                },
            ],
        })
    })

    it('prefills project from activeProjectId when defaultProjectId is not provided', async () => {
        window.localStorage.setItem('experiment_portal.active_project_id', 'project-1')

        render(<CreateExperimentModal isOpen={true} onClose={vi.fn()} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })

        const projectSelect = screen.getByLabelText(/проект/i) as HTMLSelectElement
        await waitFor(() => {
            expect(projectSelect.value).toBe('project-1')
        })
    })

    it('renders create experiment form when open', async () => {
        render(<CreateExperimentModal isOpen={true} onClose={vi.fn()} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByRole('heading', { name: /создать эксперимент/i })).toBeInTheDocument()
        })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        }, { timeout: 3000 })

        expect(screen.getByPlaceholderText('Например: Аэродинамические испытания крыла')).toBeInTheDocument()
        expect(screen.getByPlaceholderText('Детальное описание эксперимента...')).toBeInTheDocument()
    })

    it('does not render when closed', () => {
        render(<CreateExperimentModal isOpen={false} onClose={vi.fn()} />, { wrapper: createWrapper() })
        expect(screen.queryByText('Создать эксперимент')).not.toBeInTheDocument()
    })

    it('submits form with valid data', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(experimentsApi.create)

        const createdExperiment = {
            id: 'exp-123',
            project_id: 'project-1',
            name: 'New Experiment',
            description: 'Test description',
            status: 'draft' as const,
            tags: [],
            metadata: {},
            owner_id: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        }

        mockCreate.mockResolvedValueOnce(createdExperiment)

        render(<CreateExperimentModal isOpen={true} onClose={vi.fn()} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        await user.selectOptions(screen.getByLabelText(/проект/i), 'project-1')
        await user.type(screen.getByPlaceholderText('Например: Аэродинамические испытания крыла'), 'New Experiment')
        await user.type(screen.getByPlaceholderText('Детальное описание эксперимента...'), 'Test description')

        const submitButton = screen.getByRole('button', { name: /создать эксперимент/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(mockCreate).toHaveBeenCalledWith({
                project_id: 'project-1',
                name: 'New Experiment',
                description: 'Test description',
                experiment_type: undefined,
                tags: [],
                metadata: {},
            })
            expect(mockNavigate).toHaveBeenCalledWith('/experiments/exp-123')
        })
    })

    it('parses tags from comma-separated input', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(experimentsApi.create)

        mockCreate.mockResolvedValueOnce({
            id: 'exp-123',
            project_id: 'project-1',
            name: 'New Experiment',
            status: 'draft' as const,
            tags: ['tag1', 'tag2', 'tag3'],
            metadata: {},
            owner_id: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        })

        render(<CreateExperimentModal isOpen={true} onClose={vi.fn()} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        await user.selectOptions(screen.getByLabelText(/проект/i), 'project-1')
        await user.type(screen.getByPlaceholderText('Например: Аэродинамические испытания крыла'), 'New Experiment')
        await user.type(screen.getByPlaceholderText(/через запятую/i), 'tag1, tag2, tag3')

        const submitButton = screen.getByRole('button', { name: /создать эксперимент/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(mockCreate).toHaveBeenCalledWith(
                expect.objectContaining({
                    tags: ['tag1', 'tag2', 'tag3'],
                })
            )
        })
    })

    it('parses metadata from JSON input', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(experimentsApi.create)

        mockCreate.mockResolvedValueOnce({
            id: 'exp-123',
            project_id: 'project-1',
            name: 'New Experiment',
            status: 'draft' as const,
            tags: [],
            metadata: { key: 'value' },
            owner_id: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        })

        render(<CreateExperimentModal isOpen={true} onClose={vi.fn()} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        await user.selectOptions(screen.getByLabelText(/проект/i), 'project-1')
        await user.type(screen.getByPlaceholderText('Например: Аэродинамические испытания крыла'), 'New Experiment')
        const metadataLabel = screen.getByText('Метаданные (JSON)')
        const metadataTextarea = metadataLabel.parentElement?.querySelector('textarea')
        expect(metadataTextarea).toBeInTheDocument()
        await user.clear(metadataTextarea!)
        // Используем fireEvent для ввода JSON, так как type может интерпретировать специальные символы
        fireEvent.change(metadataTextarea!, { target: { value: '{"key": "value"}' } })

        const submitButton = screen.getByRole('button', { name: /создать эксперимент/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(mockCreate).toHaveBeenCalledWith(
                expect.objectContaining({
                    metadata: { key: 'value' },
                })
            )
        })
    })

    it('shows error for invalid JSON metadata', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(experimentsApi.create)

        render(<CreateExperimentModal isOpen={true} onClose={vi.fn()} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        await user.selectOptions(screen.getByLabelText(/проект/i), 'project-1')
        await user.type(screen.getByPlaceholderText('Например: Аэродинамические испытания крыла'), 'New Experiment')
        const metadataLabel = screen.getByText('Метаданные (JSON)')
        const metadataTextarea = metadataLabel.parentElement?.querySelector('textarea')
        expect(metadataTextarea).toBeInTheDocument()
        await user.clear(metadataTextarea!)
        // Используем fireEvent для ввода JSON, так как type может интерпретировать специальные символы
        fireEvent.change(metadataTextarea!, { target: { value: 'invalid json{' } })

        const submitButton = screen.getByRole('button', { name: /создать эксперимент/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(screen.getByText(/неверный формат json/i)).toBeInTheDocument()
            expect(mockCreate).not.toHaveBeenCalled()
        })
    })

    it('shows error message on create failure', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(experimentsApi.create)

        mockCreate.mockRejectedValueOnce({
            response: {
                data: { error: 'Validation error' },
            },
        })

        render(<CreateExperimentModal isOpen={true} onClose={vi.fn()} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        await user.selectOptions(screen.getByLabelText(/проект/i), 'project-1')
        await user.type(screen.getByPlaceholderText('Например: Аэродинамические испытания крыла'), 'New Experiment')

        const submitButton = screen.getByRole('button', { name: /создать эксперимент/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(mockCreate).toHaveBeenCalled()
        }, { timeout: 2000 })

        // Ждем появления ошибки - используем queryByText, так как ошибка может появиться не сразу
        await waitFor(() => {
            const errorElement = screen.queryByText('Validation error')
            expect(errorElement).toBeInTheDocument()
            expect(errorElement).toHaveClass('error')
        }, { timeout: 3000 })
    })

    it('shows generic error message on create failure without response', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(experimentsApi.create)

        mockCreate.mockRejectedValueOnce(new Error('Network error'))

        render(<CreateExperimentModal isOpen={true} onClose={vi.fn()} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        await user.selectOptions(screen.getByLabelText(/проект/i), 'project-1')
        await user.type(screen.getByPlaceholderText('Например: Аэродинамические испытания крыла'), 'New Experiment')

        const submitButton = screen.getByRole('button', { name: /создать эксперимент/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(mockCreate).toHaveBeenCalled()
        }, { timeout: 2000 })

        // Ждем появления ошибки - используем queryByText, так как ошибка может появиться не сразу
        await waitFor(() => {
            const errorElement = screen.queryByText(/ошибка создания эксперимента/i)
            expect(errorElement).toBeInTheDocument()
            expect(errorElement).toHaveClass('error')
        }, { timeout: 3000 })
    })

    it('disables submit button during submission', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(experimentsApi.create)

        let resolveCreate: (value: any) => void
        const createPromise = new Promise((resolve) => {
            resolveCreate = resolve
        })
        mockCreate.mockReturnValueOnce(createPromise as any)

        render(<CreateExperimentModal isOpen={true} onClose={vi.fn()} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        await user.selectOptions(screen.getByLabelText(/проект/i), 'project-1')
        await user.type(screen.getByPlaceholderText('Например: Аэродинамические испытания крыла'), 'New Experiment')

        const submitButton = screen.getByRole('button', { name: /создать эксперимент/i })

        // Кликаем и сразу проверяем, что кнопка стала disabled
        const clickPromise = user.click(submitButton)

        await waitFor(() => {
            const button = screen.getByRole('button', { name: /создание\.\.\./i })
            expect(button).toBeDisabled()
        })

        await clickPromise

        resolveCreate!({
            id: 'exp-123',
            project_id: 'project-1',
            name: 'New Experiment',
            status: 'draft' as const,
            tags: [],
            metadata: {},
            owner_id: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        })
    })

    it('calls onClose when cancel button is clicked', async () => {
        const user = userEvent.setup()
        const mockOnClose = vi.fn()

        render(<CreateExperimentModal isOpen={true} onClose={mockOnClose} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })

        const cancelButton = screen.getByRole('button', { name: /отмена/i })
        await user.click(cancelButton)

        expect(mockOnClose).toHaveBeenCalled()
    })

    it('allows selecting experiment type', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(experimentsApi.create)

        mockCreate.mockResolvedValueOnce({
            id: 'exp-123',
            project_id: 'project-1',
            name: 'New Experiment',
            experiment_type: 'benchmark',
            status: 'draft' as const,
            tags: [],
            metadata: {},
            owner_id: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        })

        render(<CreateExperimentModal isOpen={true} onClose={vi.fn()} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        await user.selectOptions(screen.getByLabelText(/проект/i), 'project-1')
        await user.type(screen.getByPlaceholderText('Например: Аэродинамические испытания крыла'), 'New Experiment')
        await user.selectOptions(screen.getByLabelText(/тип эксперимента/i), 'benchmark')

        const submitButton = screen.getByRole('button', { name: /создать эксперимент/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(mockCreate).toHaveBeenCalledWith(
                expect.objectContaining({
                    experiment_type: 'benchmark',
                })
            )
        })
    })

    it('filters empty tags', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(experimentsApi.create)

        mockCreate.mockResolvedValueOnce({
            id: 'exp-123',
            project_id: 'project-1',
            name: 'New Experiment',
            status: 'draft' as const,
            tags: ['tag1', 'tag2'],
            metadata: {},
            owner_id: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        })

        render(<CreateExperimentModal isOpen={true} onClose={vi.fn()} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        await user.selectOptions(screen.getByLabelText(/проект/i), 'project-1')
        await user.type(screen.getByPlaceholderText('Например: Аэродинамические испытания крыла'), 'New Experiment')
        await user.type(screen.getByPlaceholderText(/через запятую/i), 'tag1, , tag2,  ')

        const submitButton = screen.getByRole('button', { name: /создать эксперимент/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(mockCreate).toHaveBeenCalledWith(
                expect.objectContaining({
                    tags: ['tag1', 'tag2'],
                })
            )
        })
    })
})

