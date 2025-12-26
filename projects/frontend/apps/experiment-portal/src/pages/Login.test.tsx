import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import Login from './Login'
import { authApi } from '../api/auth'

// Мокаем authApi
vi.mock('../api/auth', () => ({
    authApi: {
        login: vi.fn(),
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
            <MemoryRouter>
                {children}
            </MemoryRouter>
        </QueryClientProvider>
    )
}

describe('Login', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockNavigate.mockClear()
    })

    it('renders login form', () => {
        render(<Login />, { wrapper: createWrapper() })

        expect(screen.getByRole('heading', { name: /вход в систему/i })).toBeInTheDocument()
        expect(screen.getByLabelText(/имя пользователя/i)).toBeInTheDocument()
        expect(screen.getByLabelText(/пароль/i)).toBeInTheDocument()
        expect(screen.getByRole('button', { name: /войти/i })).toBeInTheDocument()
    })

    it('shows error message on login failure', async () => {
        const user = userEvent.setup()
        const mockLogin = vi.mocked(authApi.login)
        mockLogin.mockRejectedValueOnce({
            response: {
                data: { error: 'Invalid credentials' },
            },
        })

        render(<Login />, { wrapper: createWrapper() })

        await user.type(screen.getByLabelText(/имя пользователя/i), 'testuser')
        await user.type(screen.getByLabelText(/пароль/i), 'wrongpassword')
        await user.click(screen.getByRole('button', { name: /войти/i }))

        await waitFor(() => {
            expect(screen.getByText(/invalid credentials/i)).toBeInTheDocument()
        })
    })

    it('submits form with correct credentials', async () => {
        const user = userEvent.setup()
        const mockLogin = vi.mocked(authApi.login)
        mockLogin.mockResolvedValueOnce({
            expires_in: 900,
            token_type: 'bearer',
        })

        render(<Login />, { wrapper: createWrapper() })

        await user.type(screen.getByLabelText(/имя пользователя/i), 'testuser')
        await user.type(screen.getByLabelText(/пароль/i), 'password123')
        await user.click(screen.getByRole('button', { name: /войти/i }))

        await waitFor(() => {
            expect(mockLogin).toHaveBeenCalledWith({
                username: 'testuser',
                password: 'password123',
            })
            expect(mockNavigate).toHaveBeenCalledWith('/experiments')
        })
    })

    it('disables form during submission', async () => {
        const user = userEvent.setup()
        const mockLogin = vi.mocked(authApi.login)
        let resolveLogin: (value: any) => void
        const loginPromise = new Promise((resolve) => {
            resolveLogin = resolve
        })
        mockLogin.mockReturnValueOnce(loginPromise as any)

        render(<Login />, { wrapper: createWrapper() })

        await user.type(screen.getByLabelText(/имя пользователя/i), 'testuser')
        await user.type(screen.getByLabelText(/пароль/i), 'password123')
        await user.click(screen.getByRole('button', { name: /войти/i }))

        await waitFor(() => {
            expect(screen.getByRole('button', { name: /вход\.\.\./i })).toBeDisabled()
        })

        resolveLogin!({ expires_in: 900 })
    })

    it('validates required fields', async () => {
        const user = userEvent.setup()
        render(<Login />, { wrapper: createWrapper() })

        const submitButton = screen.getByRole('button', { name: /войти/i })
        await user.click(submitButton)

        // HTML5 validation должна сработать
        const usernameInput = screen.getByLabelText(/имя пользователя/i) as HTMLInputElement
        expect(usernameInput.validity.valueMissing).toBe(true)
    })
})

