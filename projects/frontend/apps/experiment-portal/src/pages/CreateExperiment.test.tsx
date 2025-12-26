import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor, fireEvent } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import CreateExperiment from './CreateExperiment'
import { experimentsApi } from '../api/client'

// Мокаем experimentsApi
vi.mock('../api/client', () => ({
    experimentsApi: {
        create: vi.fn(),
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

describe('CreateExperiment', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockNavigate.mockClear()
    })

    it('renders create experiment form', () => {
        render(<CreateExperiment />, { wrapper: createWrapper() })

        // "Создать эксперимент" встречается и в заголовке, и в кнопке
        const createTexts = screen.getAllByText('Создать эксперимент')
        expect(createTexts.length).toBeGreaterThan(0)
        expect(screen.getByPlaceholderText('UUID проекта')).toBeInTheDocument()
        expect(screen.getByPlaceholderText('Например: Аэродинамические испытания крыла')).toBeInTheDocument()
        expect(screen.getByPlaceholderText('Детальное описание эксперимента...')).toBeInTheDocument()
    })

    it('submits form with valid data', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(experimentsApi.create)

        const createdExperiment = {
            id: 'exp-123',
            project_id: 'project-1',
            name: 'New Experiment',
            description: 'Test description',
            status: 'created' as const,
            tags: [],
            metadata: {},
            created_by: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        }

        mockCreate.mockResolvedValueOnce(createdExperiment)

        render(<CreateExperiment />, { wrapper: createWrapper() })

        await user.type(screen.getByPlaceholderText('UUID проекта'), 'project-1')
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
            status: 'created' as const,
            tags: ['tag1', 'tag2', 'tag3'],
            metadata: {},
            created_by: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        })

        render(<CreateExperiment />, { wrapper: createWrapper() })

        await user.type(screen.getByPlaceholderText('UUID проекта'), 'project-1')
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
            status: 'created' as const,
            tags: [],
            metadata: { key: 'value' },
            created_by: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        })

        render(<CreateExperiment />, { wrapper: createWrapper() })

        await user.type(screen.getByPlaceholderText('UUID проекта'), 'project-1')
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

        render(<CreateExperiment />, { wrapper: createWrapper() })

        await user.type(screen.getByPlaceholderText('UUID проекта'), 'project-1')
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

        render(<CreateExperiment />, { wrapper: createWrapper() })

        await user.type(screen.getByPlaceholderText('UUID проекта'), 'project-1')
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

        render(<CreateExperiment />, { wrapper: createWrapper() })

        await user.type(screen.getByPlaceholderText('UUID проекта'), 'project-1')
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

        render(<CreateExperiment />, { wrapper: createWrapper() })

        await user.type(screen.getByPlaceholderText('UUID проекта'), 'project-1')
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
            status: 'created' as const,
            tags: [],
            metadata: {},
            created_by: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        })
    })

    it('navigates back on cancel', async () => {
        const user = userEvent.setup()

        render(<CreateExperiment />, { wrapper: createWrapper() })

        const cancelButton = screen.getByRole('button', { name: /отмена/i })
        await user.click(cancelButton)

        expect(mockNavigate).toHaveBeenCalledWith('/experiments')
    })

    it('allows selecting experiment type', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(experimentsApi.create)

        mockCreate.mockResolvedValueOnce({
            id: 'exp-123',
            project_id: 'project-1',
            name: 'New Experiment',
            experiment_type: 'aerodynamics',
            status: 'created' as const,
            tags: [],
            metadata: {},
            created_by: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        })

        render(<CreateExperiment />, { wrapper: createWrapper() })

        // Используем placeholder, так как label не связан с input через for
        await user.type(screen.getByPlaceholderText('UUID проекта'), 'project-1')
        await user.type(screen.getByPlaceholderText('Например: Аэродинамические испытания крыла'), 'New Experiment')
        const typeLabel = screen.getByText('Тип эксперимента')
        const typeSelect = typeLabel.parentElement?.querySelector('select')
        expect(typeSelect).toBeInTheDocument()
        await user.selectOptions(typeSelect!, 'aerodynamics')

        const submitButton = screen.getByRole('button', { name: /создать эксперимент/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(mockCreate).toHaveBeenCalledWith(
                expect.objectContaining({
                    experiment_type: 'aerodynamics',
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
            status: 'created' as const,
            tags: ['tag1', 'tag2'],
            metadata: {},
            created_by: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        })

        render(<CreateExperiment />, { wrapper: createWrapper() })

        // Используем placeholder, так как label не связан с input через for
        await user.type(screen.getByPlaceholderText('UUID проекта'), 'project-1')
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

