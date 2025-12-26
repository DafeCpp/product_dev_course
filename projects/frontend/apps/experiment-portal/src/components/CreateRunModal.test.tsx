import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import CreateRunModal from './CreateRunModal'
import { runsApi } from '../api/client'

// Мокаем runsApi
vi.mock('../api/client', () => ({
    runsApi: {
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

describe('CreateRunModal', () => {
    const mockOnClose = vi.fn()

    beforeEach(() => {
        vi.clearAllMocks()
        mockNavigate.mockClear()
        mockOnClose.mockClear()
    })

    it('does not render when isOpen is false', () => {
        render(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={false}
                onClose={mockOnClose}
            />,
            { wrapper: createWrapper() }
        )

        expect(screen.queryByRole('heading', { name: /создать новый запуск/i })).not.toBeInTheDocument()
    })

    it('renders modal when isOpen is true', () => {
        render(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={true}
                onClose={mockOnClose}
            />,
            { wrapper: createWrapper() }
        )

        expect(screen.getByRole('heading', { name: /создать новый запуск/i })).toBeInTheDocument()
        expect(screen.getByLabelText(/название/i)).toBeInTheDocument()
        expect(screen.getByLabelText(/параметры/i)).toBeInTheDocument()
        expect(screen.getByLabelText(/заметки/i)).toBeInTheDocument()
        expect(screen.getByLabelText(/метаданные/i)).toBeInTheDocument()
    })

    it('closes modal when close button is clicked', async () => {
        const user = userEvent.setup()
        render(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={true}
                onClose={mockOnClose}
            />,
            { wrapper: createWrapper() }
        )

        const closeButton = screen.getByRole('button', { name: /×/i })
        await user.click(closeButton)

        expect(mockOnClose).toHaveBeenCalledTimes(1)
    })

    it('closes modal when overlay is clicked', async () => {
        const user = userEvent.setup()
        render(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={true}
                onClose={mockOnClose}
            />,
            { wrapper: createWrapper() }
        )

        const overlay = screen.getByRole('heading', { name: /создать новый запуск/i }).closest('.modal-overlay')
        if (overlay) {
            await user.click(overlay)
            expect(mockOnClose).toHaveBeenCalledTimes(1)
        }
    })

    it('does not close modal when clicking inside modal content', async () => {
        const user = userEvent.setup()
        render(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={true}
                onClose={mockOnClose}
            />,
            { wrapper: createWrapper() }
        )

        const form = screen.getByRole('heading', { name: /создать новый запуск/i }).closest('.modal-content')
        if (form) {
            await user.click(form)
            expect(mockOnClose).not.toHaveBeenCalled()
        }
    })

    it('validates required name field', async () => {
        const user = userEvent.setup()
        render(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={true}
                onClose={mockOnClose}
            />,
            { wrapper: createWrapper() }
        )

        const submitButton = screen.getByRole('button', { name: /создать запуск/i })
        await user.click(submitButton)

        // HTML5 validation should prevent submission
        const nameInput = screen.getByLabelText(/название/i) as HTMLInputElement
        expect(nameInput.validity.valueMissing).toBe(true)
    })

    it('submits form with correct data', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(runsApi.create)
        const mockRun = {
            id: 'run-1',
            experiment_id: 'exp-1',
            name: 'Test Run',
            params: { key: 'value' },
            status: 'draft' as const,
            notes: 'Test notes',
            metadata: { meta: 'data' },
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        }
        mockCreate.mockResolvedValueOnce(mockRun)

        render(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={true}
                onClose={mockOnClose}
            />,
            { wrapper: createWrapper() }
        )

        await user.type(screen.getByLabelText(/название/i), 'Test Run')
        const parametersTextarea = screen.getByLabelText(/параметры/i)
        await user.clear(parametersTextarea)
        await user.paste('{"key": "value"}')
        await user.type(screen.getByLabelText(/заметки/i), 'Test notes')
        const metadataTextarea = screen.getByLabelText(/метаданные/i)
        await user.clear(metadataTextarea)
        await user.paste('{"meta": "data"}')

        const submitButton = screen.getByRole('button', { name: /создать запуск/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(mockCreate).toHaveBeenCalledWith('exp-1', {
                name: 'Test Run',
                params: { key: 'value' },
                notes: 'Test notes',
                metadata: { meta: 'data' },
            })
        })

        await waitFor(() => {
            expect(mockNavigate).toHaveBeenCalledWith('/runs/run-1')
        })

        expect(mockOnClose).toHaveBeenCalledTimes(1)
    })

    it('handles empty JSON fields', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(runsApi.create)
        const mockRun = {
            id: 'run-1',
            experiment_id: 'exp-1',
            name: 'Test Run',
            params: {},
            status: 'draft' as const,
            metadata: {},
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        }
        mockCreate.mockResolvedValueOnce(mockRun)

        render(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={true}
                onClose={mockOnClose}
            />,
            { wrapper: createWrapper() }
        )

        await user.type(screen.getByLabelText(/название/i), 'Test Run')

        const submitButton = screen.getByRole('button', { name: /создать запуск/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(mockCreate).toHaveBeenCalledWith('exp-1', {
                name: 'Test Run',
                params: {},
                notes: undefined,
                metadata: undefined,
            })
        })
    })

    it('shows error for invalid JSON in parameters', async () => {
        const user = userEvent.setup()
        render(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={true}
                onClose={mockOnClose}
            />,
            { wrapper: createWrapper() }
        )

        await user.type(screen.getByLabelText(/название/i), 'Test Run')
        const parametersTextarea = screen.getByLabelText(/параметры/i)
        await user.clear(parametersTextarea)
        await user.paste('{invalid json}')

        const submitButton = screen.getByRole('button', { name: /создать запуск/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(screen.getByText(/ошибка в формате json для параметров/i)).toBeInTheDocument()
        })

        expect(vi.mocked(runsApi.create)).not.toHaveBeenCalled()
    })

    it('shows error for invalid JSON in metadata', async () => {
        const user = userEvent.setup()
        render(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={true}
                onClose={mockOnClose}
            />,
            { wrapper: createWrapper() }
        )

        await user.type(screen.getByLabelText(/название/i), 'Test Run')
        const metadataTextarea = screen.getByLabelText(/метаданные/i)
        await user.clear(metadataTextarea)
        await user.paste('{invalid json}')

        const submitButton = screen.getByRole('button', { name: /создать запуск/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(screen.getByText(/ошибка в формате json для метаданных/i)).toBeInTheDocument()
        })

        expect(vi.mocked(runsApi.create)).not.toHaveBeenCalled()
    })

    it('shows error on API failure', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(runsApi.create)
        mockCreate.mockRejectedValueOnce({
            response: {
                data: { error: 'Run name already exists' },
            },
        })

        render(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={true}
                onClose={mockOnClose}
            />,
            { wrapper: createWrapper() }
        )

        await user.type(screen.getByLabelText(/название/i), 'Test Run')

        const submitButton = screen.getByRole('button', { name: /создать запуск/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(screen.getByText(/run name already exists/i)).toBeInTheDocument()
        })
    })

    it('disables form during submission', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(runsApi.create)
        let resolvePromise: (value: any) => void
        const promise = new Promise((resolve) => {
            resolvePromise = resolve
        })
        mockCreate.mockReturnValueOnce(promise as any)

        render(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={true}
                onClose={mockOnClose}
            />,
            { wrapper: createWrapper() }
        )

        await user.type(screen.getByLabelText(/название/i), 'Test Run')

        const submitButton = screen.getByRole('button', { name: /создать запуск/i })
        const nameInput = screen.getByLabelText(/название/i) as HTMLInputElement

        // Кликаем и проверяем состояние
        const clickPromise = user.click(submitButton)

        // Ждем, пока форма станет disabled
        await waitFor(() => {
            expect(submitButton).toBeDisabled()
            expect(nameInput).toBeDisabled()
        }, { timeout: 2000 })

        // Завершаем промис
        resolvePromise!({
            id: 'run-1',
            experiment_id: 'exp-1',
            name: 'Test Run',
            params: {},
            status: 'draft' as const,
            metadata: {},
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        })

        await clickPromise
    })

    it('resets form when modal is closed', async () => {
        const user = userEvent.setup()
        const { rerender } = render(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={true}
                onClose={mockOnClose}
            />,
            { wrapper: createWrapper() }
        )

        await user.type(screen.getByLabelText(/название/i), 'Test Run')
        expect(screen.getByLabelText(/название/i)).toHaveValue('Test Run')

        // Закрываем модальное окно
        const closeButton = screen.getByRole('button', { name: /×/i })
        await user.click(closeButton)

        // Открываем снова
        rerender(
            <CreateRunModal
                experimentId="exp-1"
                isOpen={true}
                onClose={mockOnClose}
            />
        )

        expect(screen.getByLabelText(/название/i)).toHaveValue('')
    })
})

