import { describe, expect, it, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import Layout from './Layout'
import { authApi } from '../api/auth'

// Мокаем authApi
vi.mock('../api/auth', () => ({
    authApi: {
        me: vi.fn(),
        logout: vi.fn(),
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
            <MemoryRouter initialEntries={['/experiments']}>
                {children}
            </MemoryRouter>
        </QueryClientProvider>
    )
}

describe('Layout', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        const mockMe = vi.mocked(authApi.me)
        mockMe.mockResolvedValue({
            id: '1',
            username: 'testuser',
            email: 'test@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })
    })

    it('renders header links and page content', async () => {
        render(
            <Layout>
                <div>Test content</div>
            </Layout>,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(
                screen.getByRole('heading', {
                    name: /experiment tracking/i,
                })
            ).toBeInTheDocument()
        })

        expect(screen.getByText('Test content')).toBeInTheDocument()
        expect(screen.getByRole('link', { name: /эксперименты/i })).toHaveClass('active')
    })

    it('displays username when user is authenticated', async () => {
        render(
            <Layout>
                <div>Test content</div>
            </Layout>,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('testuser')).toBeInTheDocument()
        })
    })

    it('renders logout button', async () => {
        render(
            <Layout>
                <div>Test content</div>
            </Layout>,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByRole('button', { name: /выйти/i })).toBeInTheDocument()
        })
    })
})
