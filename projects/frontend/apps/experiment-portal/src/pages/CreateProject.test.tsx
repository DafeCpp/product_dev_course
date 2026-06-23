import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import CreateProject from './CreateProject'
import { projectsApi } from '../api/client'

vi.mock('../api/client', () => ({
    projectsApi: {
        create: vi.fn(),
    },
}))

const mockNavigate = vi.fn()
vi.mock('react-router-dom', async () => {
    const actual = await vi.importActual<typeof import('react-router-dom')>('react-router-dom')
    return {
        ...actual,
        useNavigate: () => mockNavigate,
    }
})

vi.mock('../utils/notify', () => ({
    notifyError: vi.fn(),
    notifySuccess: vi.fn(),
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

describe('CreateProject', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockNavigate.mockClear()
    })

    it('renders form fields and action buttons', () => {
        render(<CreateProject />, { wrapper: createWrapper() })

        expect(screen.getByRole('heading', { name: /создать проект/i })).toBeInTheDocument()
        expect(screen.getByPlaceholderText(/аэродинамические испытания/i)).toBeInTheDocument()
        expect(screen.getByPlaceholderText(/описание проекта/i)).toBeInTheDocument()
        expect(screen.getByRole('button', { name: /отмена/i })).toBeInTheDocument()
        expect(screen.getByRole('button', { name: /^создать проект$/i })).toBeInTheDocument()
    })

    it('navigates to /projects when "Отмена" is clicked', async () => {
        const user = userEvent.setup()
        render(<CreateProject />, { wrapper: createWrapper() })

        await user.click(screen.getByRole('button', { name: /отмена/i }))
        expect(mockNavigate).toHaveBeenCalledWith('/projects')
    })

    it('submits trimmed name and description on success and navigates to /projects', async () => {
        vi.mocked(projectsApi.create).mockResolvedValueOnce({ id: 'p1', name: 'My Project' } as any)
        const user = userEvent.setup()
        render(<CreateProject />, { wrapper: createWrapper() })

        await user.type(screen.getByPlaceholderText(/аэродинамические испытания/i), '  My Project  ')
        await user.type(screen.getByPlaceholderText(/описание проекта/i), 'Some description')
        await user.click(screen.getByRole('button', { name: /^создать проект$/i }))

        await waitFor(() => {
            expect(vi.mocked(projectsApi.create)).toHaveBeenCalledWith({
                name: 'My Project',
                description: 'Some description',
            })
            expect(mockNavigate).toHaveBeenCalledWith('/projects')
        })
    })

    it('omits description when empty', async () => {
        vi.mocked(projectsApi.create).mockResolvedValueOnce({ id: 'p1', name: 'My Project' } as any)
        const user = userEvent.setup()
        render(<CreateProject />, { wrapper: createWrapper() })

        await user.type(screen.getByPlaceholderText(/аэродинамические испытания/i), 'My Project')
        await user.click(screen.getByRole('button', { name: /^создать проект$/i }))

        await waitFor(() => {
            expect(vi.mocked(projectsApi.create)).toHaveBeenCalledWith({
                name: 'My Project',
                description: undefined,
            })
        })
    })

    it('disables submit while pending', async () => {
        let resolveCreate: (v: any) => void
        const pending = new Promise((resolve) => {
            resolveCreate = resolve
        })
        vi.mocked(projectsApi.create).mockReturnValueOnce(pending as any)
        const user = userEvent.setup()
        render(<CreateProject />, { wrapper: createWrapper() })

        await user.type(screen.getByPlaceholderText(/аэродинамические испытания/i), 'My Project')
        await user.click(screen.getByRole('button', { name: /^создать проект$/i }))

        await waitFor(() => {
            expect(screen.getByRole('button', { name: /создание/i })).toBeDisabled()
        })

        resolveCreate!({ id: 'p1' })
    })

    it('does not navigate on API error', async () => {
        vi.mocked(projectsApi.create).mockRejectedValueOnce({
            response: { data: { error: 'Project name taken' } },
        })
        const user = userEvent.setup()
        render(<CreateProject />, { wrapper: createWrapper() })

        await user.type(screen.getByPlaceholderText(/аэродинамические испытания/i), 'Duplicate')
        await user.click(screen.getByRole('button', { name: /^создать проект$/i }))

        await waitFor(() => {
            expect(vi.mocked(projectsApi.create)).toHaveBeenCalled()
        })
        expect(mockNavigate).not.toHaveBeenCalledWith('/projects')
    })
})
