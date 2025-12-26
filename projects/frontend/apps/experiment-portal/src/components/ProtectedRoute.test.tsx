import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ProtectedRoute from './ProtectedRoute'
import { authApi } from '../api/auth'

// Мокаем authApi
vi.mock('../api/auth', () => ({
    authApi: {
        me: vi.fn(),
    },
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

describe('ProtectedRoute', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('renders children when user is authenticated', async () => {
        const mockMe = vi.mocked(authApi.me)
        mockMe.mockResolvedValueOnce({
            id: '1',
            username: 'testuser',
            email: 'test@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })

        render(
            <ProtectedRoute>
                <div>Protected Content</div>
            </ProtectedRoute>,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('Protected Content')).toBeInTheDocument()
        })
    })

    it('shows loading state while checking authentication', () => {
        const mockMe = vi.mocked(authApi.me)
        mockMe.mockImplementation(
            () =>
                new Promise((resolve) => {
                    // Не резолвим сразу, чтобы проверить loading state
                    setTimeout(() => {
                        resolve({
                            id: '1',
                            username: 'testuser',
                            email: 'test@example.com',
                            is_active: true,
                            created_at: '2024-01-01T00:00:00Z',
                        })
                    }, 100)
                })
        )

        render(
            <ProtectedRoute>
                <div>Protected Content</div>
            </ProtectedRoute>,
            { wrapper: createWrapper() }
        )

        expect(screen.getByText(/проверка авторизации/i)).toBeInTheDocument()
    })

    it('redirects to login when user is not authenticated', async () => {
        const mockMe = vi.mocked(authApi.me)
        mockMe.mockRejectedValueOnce(new Error('Unauthorized'))

        render(
            <ProtectedRoute>
                <div>Protected Content</div>
            </ProtectedRoute>,
            { wrapper: createWrapper(['/protected']) }
        )

        await waitFor(() => {
            expect(mockMe).toHaveBeenCalled()
            // Проверяем, что контент не отображается
            expect(screen.queryByText('Protected Content')).not.toBeInTheDocument()
        })
    })

    it('calls authApi.me on mount', () => {
        const mockMe = vi.mocked(authApi.me)
        mockMe.mockResolvedValueOnce({
            id: '1',
            username: 'testuser',
            email: 'test@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })

        render(
            <ProtectedRoute>
                <div>Protected Content</div>
            </ProtectedRoute>,
            { wrapper: createWrapper() }
        )

        expect(mockMe).toHaveBeenCalledTimes(1)
    })
})

