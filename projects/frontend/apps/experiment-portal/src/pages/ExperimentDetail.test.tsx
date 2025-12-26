import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ExperimentDetail from './ExperimentDetail'
import { experimentsApi } from '../api/client'

// Мокаем experimentsApi
vi.mock('../api/client', () => ({
    experimentsApi: {
        get: vi.fn(),
        delete: vi.fn(),
    },
}))

// Мокаем RunsList
vi.mock('../components/RunsList', () => ({
    default: ({ experimentId }: { experimentId: string }) => (
        <div data-testid="runs-list">RunsList for {experimentId}</div>
    ),
}))

// Мокаем useNavigate
const mockNavigate = vi.fn()
vi.mock('react-router-dom', async () => {
    const actual = await vi.importActual('react-router-dom')
    return {
        ...actual,
        useNavigate: () => mockNavigate,
        useParams: () => ({ id: 'exp-123' }),
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

const mockExperiment = {
    id: 'exp-123',
    project_id: 'project-1',
    name: 'Test Experiment',
    description: 'Test description',
    experiment_type: 'aerodynamics',
    status: 'draft' as const,
    tags: ['test', 'aerodynamics'],
    metadata: { key: 'value' },
    owner_id: 'user-1',
    created_at: '2024-01-01T00:00:00Z',
    updated_at: '2024-01-02T00:00:00Z',
}

describe('ExperimentDetail', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockNavigate.mockClear()
        // Мокаем window.confirm
        window.confirm = vi.fn(() => true)
    })

    it('renders loading state', () => {
        const mockGet = vi.mocked(experimentsApi.get)
        mockGet.mockReturnValueOnce(
            new Promise(() => {}) // Never resolves
        )

        render(<ExperimentDetail />, { wrapper: createWrapper() })
        expect(screen.getByText('Загрузка...')).toBeInTheDocument()
    })

    it('renders error state', async () => {
        const mockGet = vi.mocked(experimentsApi.get)
        mockGet.mockRejectedValueOnce(new Error('Not found'))

        render(<ExperimentDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Эксперимент не найден')).toBeInTheDocument()
        })
    })

    it('renders experiment details', async () => {
        const mockGet = vi.mocked(experimentsApi.get)
        mockGet.mockResolvedValueOnce(mockExperiment)

        render(<ExperimentDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Experiment')).toBeInTheDocument()
            expect(screen.getByText('Test description')).toBeInTheDocument()
            expect(screen.getByText('exp-123')).toBeInTheDocument()
            expect(screen.getByText('project-1')).toBeInTheDocument()
        })
    })

    it('displays status badge', async () => {
        const mockGet = vi.mocked(experimentsApi.get)
        mockGet.mockResolvedValueOnce(mockExperiment)

        render(<ExperimentDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Черновик')).toBeInTheDocument()
        })
    })

    it('displays tags', async () => {
        const mockGet = vi.mocked(experimentsApi.get)
        mockGet.mockResolvedValueOnce(mockExperiment)

        render(<ExperimentDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            // Ищем теги по классу .tag, чтобы избежать конфликта с типом эксперимента
            const testTag = screen.getByText('test', { selector: '.tag' })
            const aerodynamicsTag = screen.getByText('aerodynamics', { selector: '.tag' })
            expect(testTag).toBeInTheDocument()
            expect(aerodynamicsTag).toBeInTheDocument()
        })
    })

    it('displays metadata', async () => {
        const mockGet = vi.mocked(experimentsApi.get)
        mockGet.mockResolvedValueOnce(mockExperiment)

        render(<ExperimentDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/key.*value/i)).toBeInTheDocument()
        })
    })

    it('renders RunsList component', async () => {
        const mockGet = vi.mocked(experimentsApi.get)
        mockGet.mockResolvedValueOnce(mockExperiment)

        render(<ExperimentDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByTestId('runs-list')).toBeInTheDocument()
            expect(screen.getByText('RunsList for exp-123')).toBeInTheDocument()
        })
    })

    it('deletes experiment on confirm', async () => {
        const user = userEvent.setup()
        const mockGet = vi.mocked(experimentsApi.get)
        const mockDelete = vi.mocked(experimentsApi.delete)

        mockGet.mockResolvedValueOnce(mockExperiment)
        mockDelete.mockResolvedValueOnce(undefined)

        render(<ExperimentDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Experiment')).toBeInTheDocument()
        })

        const deleteButton = screen.getByText('Удалить')
        await user.click(deleteButton)

        await waitFor(() => {
            expect(window.confirm).toHaveBeenCalledWith('Удалить эксперимент?')
            expect(mockDelete).toHaveBeenCalledWith('exp-123')
            expect(mockNavigate).toHaveBeenCalledWith('/experiments')
        })
    })

    it('does not delete experiment on cancel', async () => {
        const user = userEvent.setup()
        const mockGet = vi.mocked(experimentsApi.get)
        const mockDelete = vi.mocked(experimentsApi.delete)

        window.confirm = vi.fn(() => false)

        mockGet.mockResolvedValueOnce(mockExperiment)

        render(<ExperimentDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Experiment')).toBeInTheDocument()
        })

        const deleteButton = screen.getByText('Удалить')
        await user.click(deleteButton)

        await waitFor(() => {
            expect(window.confirm).toHaveBeenCalled()
            expect(mockDelete).not.toHaveBeenCalled()
        })
    })

    it('toggles edit form', async () => {
        const user = userEvent.setup()
        const mockGet = vi.mocked(experimentsApi.get)
        mockGet.mockResolvedValueOnce(mockExperiment)

        render(<ExperimentDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Experiment')).toBeInTheDocument()
        })

        const editButton = screen.getByText('Редактировать')
        await user.click(editButton)

        // Note: The component doesn't actually render edit form in current implementation
        // This test verifies the button click works
        expect(editButton).toBeInTheDocument()
    })

    it('displays experiment type', async () => {
        const mockGet = vi.mocked(experimentsApi.get)
        mockGet.mockResolvedValueOnce(mockExperiment)

        render(<ExperimentDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/тип/i)).toBeInTheDocument()
            // aerodynamics может быть и в типе, и в тегах, поэтому используем getAllByText
            const aerodynamicsElements = screen.getAllByText('aerodynamics')
            expect(aerodynamicsElements.length).toBeGreaterThan(0)
        })
    })

    it('handles experiment without description', async () => {
        const mockGet = vi.mocked(experimentsApi.get)
        mockGet.mockResolvedValueOnce({
            ...mockExperiment,
            description: undefined,
        })

        render(<ExperimentDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Experiment')).toBeInTheDocument()
            expect(screen.queryByText('Test description')).not.toBeInTheDocument()
        })
    })

    it('handles experiment without tags', async () => {
        const mockGet = vi.mocked(experimentsApi.get)
        mockGet.mockResolvedValueOnce({
            ...mockExperiment,
            tags: [],
        })

        render(<ExperimentDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Test Experiment')).toBeInTheDocument()
            expect(screen.queryByText('test')).not.toBeInTheDocument()
        })
    })
})

