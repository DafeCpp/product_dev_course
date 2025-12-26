import { describe, it, expect, vi } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import App from './App'
import { authApi } from './api/auth'

// Мокаем authApi
vi.mock('./api/auth', () => ({
    authApi: {
        me: vi.fn(),
    },
}))

// Мокаем все страницы
vi.mock('./pages/Login', () => ({
    default: () => <div>Login Page</div>,
}))

vi.mock('./pages/ExperimentsList', () => ({
    default: () => <div>ExperimentsList Page</div>,
}))

vi.mock('./pages/CreateExperiment', () => ({
    default: () => <div>CreateExperiment Page</div>,
}))

vi.mock('./pages/ExperimentDetail', () => ({
    default: () => <div>ExperimentDetail Page</div>,
}))

vi.mock('./pages/RunDetail', () => ({
    default: () => <div>RunDetail Page</div>,
}))

vi.mock('./components/Layout', () => ({
    default: ({ children }: { children: React.ReactNode }) => (
        <div data-testid="layout">{children}</div>
    ),
}))

vi.mock('./components/ProtectedRoute', () => ({
    default: ({ children }: { children: React.ReactNode }) => (
        <div data-testid="protected-route">{children}</div>
    ),
}))

const createWrapper = (initialEntries = ['/']) => {
    const queryClient = new QueryClient({
        defaultOptions: {
            queries: { retry: false },
            mutations: { retry: false },
        },
    })
    return ({ children }: { children: React.ReactNode }) => (
        <QueryClientProvider client={queryClient}>
            <MemoryRouter initialEntries={initialEntries}>
                {children}
            </MemoryRouter>
        </QueryClientProvider>
    )
}

describe('App', () => {
    it('renders login page at /login', () => {
        const mockMe = vi.mocked(authApi.me)
        mockMe.mockResolvedValueOnce({
            id: '1',
            username: 'testuser',
            email: 'test@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })

        render(<App />, { wrapper: createWrapper(['/login']) })
        expect(screen.getByText('Login Page')).toBeInTheDocument()
    })

    it('redirects root to /experiments', () => {
        const mockMe = vi.mocked(authApi.me)
        mockMe.mockResolvedValueOnce({
            id: '1',
            username: 'testuser',
            email: 'test@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })

        render(<App />, { wrapper: createWrapper(['/']) })
        expect(screen.getByText('ExperimentsList Page')).toBeInTheDocument()
    })

    it('renders experiments list at /experiments', () => {
        const mockMe = vi.mocked(authApi.me)
        mockMe.mockResolvedValueOnce({
            id: '1',
            username: 'testuser',
            email: 'test@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })

        render(<App />, { wrapper: createWrapper(['/experiments']) })
        expect(screen.getByText('ExperimentsList Page')).toBeInTheDocument()
    })

    it('renders create experiment page at /experiments/new', () => {
        const mockMe = vi.mocked(authApi.me)
        mockMe.mockResolvedValueOnce({
            id: '1',
            username: 'testuser',
            email: 'test@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })

        render(<App />, { wrapper: createWrapper(['/experiments/new']) })
        expect(screen.getByText('CreateExperiment Page')).toBeInTheDocument()
    })

    it('renders experiment detail at /experiments/:id', () => {
        const mockMe = vi.mocked(authApi.me)
        mockMe.mockResolvedValueOnce({
            id: '1',
            username: 'testuser',
            email: 'test@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })

        render(<App />, { wrapper: createWrapper(['/experiments/123']) })
        expect(screen.getByText('ExperimentDetail Page')).toBeInTheDocument()
    })

    it('renders run detail at /runs/:id', () => {
        const mockMe = vi.mocked(authApi.me)
        mockMe.mockResolvedValueOnce({
            id: '1',
            username: 'testuser',
            email: 'test@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })

        render(<App />, { wrapper: createWrapper(['/runs/456']) })
        expect(screen.getByText('RunDetail Page')).toBeInTheDocument()
    })

    it('wraps protected routes with ProtectedRoute and Layout', () => {
        const mockMe = vi.mocked(authApi.me)
        mockMe.mockResolvedValueOnce({
            id: '1',
            username: 'testuser',
            email: 'test@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })

        render(<App />, { wrapper: createWrapper(['/experiments']) })
        expect(screen.getByTestId('protected-route')).toBeInTheDocument()
        expect(screen.getByTestId('layout')).toBeInTheDocument()
    })
})

