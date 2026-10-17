import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import Login from './Login'
import Register from './Register'
import { authApi } from '../api/auth'
import { emitToast } from '../utils/toastBus'

// Production mode: IS_TEST is false, so notify* really emits toasts.
// Only text toasts are counted: the transport-level debug toast (HTTP status,
// trace_id) comes from the axios interceptor, which these tests mock out.
vi.mock('../utils/env', () => ({ IS_TEST: false }))
vi.mock('../utils/toastBus', () => ({ emitToast: vi.fn() }))
vi.mock('../api/auth', () => ({
    authApi: { login: vi.fn(), register: vi.fn() },
}))
vi.mock('react-router-dom', async () => {
    const actual = await vi.importActual('react-router-dom')
    return { ...actual, useNavigate: () => vi.fn() }
})

const createWrapper = () => {
    const queryClient = new QueryClient({
        defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
    })
    return ({ children }: { children: React.ReactNode }) => (
        <QueryClientProvider client={queryClient}>
            <MemoryRouter>{children}</MemoryRouter>
        </QueryClientProvider>
    )
}

const errorToasts = () =>
    vi.mocked(emitToast).mock.calls.filter(([evt]) => evt.kind === 'text' && evt.variant === 'error')

describe('auth errors in production mode', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('Login shows the server error inline without a duplicate text toast', async () => {
        const user = userEvent.setup()
        vi.mocked(authApi.login).mockRejectedValueOnce({
            response: { status: 401, data: { error: 'Invalid credentials' } },
        })
        render(<Login />, { wrapper: createWrapper() })

        await user.type(screen.getByLabelText(/имя пользователя/i), 'testuser')
        await user.type(screen.getByLabelText(/пароль/i), 'wrong')
        await user.click(screen.getByRole('button', { name: /войти/i }))

        expect(await screen.findByRole('alert')).toHaveTextContent('Invalid credentials')
        expect(errorToasts()).toHaveLength(0)
    })

    it('Login falls back to a generic message for a 4xx HTML error page', async () => {
        const user = userEvent.setup()
        vi.mocked(authApi.login).mockRejectedValueOnce({
            response: { status: 404, data: { error: '<html><body>Not Found</body></html>' } },
        })
        render(<Login />, { wrapper: createWrapper() })

        await user.type(screen.getByLabelText(/имя пользователя/i), 'testuser')
        await user.type(screen.getByLabelText(/пароль/i), 'secret')
        await user.click(screen.getByRole('button', { name: /войти/i }))

        expect(await screen.findByRole('alert')).toHaveTextContent(/ошибка входа/i)
    })

    it('Register shows a validation error inline without a toast', async () => {
        const user = userEvent.setup()
        render(<Register />, { wrapper: createWrapper() })

        await user.type(screen.getByLabelText(/имя пользователя/i), 'ab')
        await user.type(screen.getByLabelText(/^email$/i), 'a@b.co')
        await user.type(screen.getByLabelText(/^пароль$/i), 'longenough')
        await user.type(screen.getByLabelText(/повторите пароль/i), 'longenough')
        await user.click(screen.getByRole('button', { name: /зарегистрироваться/i }))

        expect(await screen.findByRole('alert')).toHaveTextContent(/не короче 3/i)
        expect(errorToasts()).toHaveLength(0)
        expect(authApi.register).not.toHaveBeenCalled()
    })

    it('Register shows the server error inline', async () => {
        const user = userEvent.setup()
        vi.mocked(authApi.register).mockRejectedValueOnce({
            response: { status: 409, data: { error: 'Username already taken' } },
        })
        render(<Register />, { wrapper: createWrapper() })

        await user.type(screen.getByLabelText(/имя пользователя/i), 'alice')
        await user.type(screen.getByLabelText(/^email$/i), 'alice@example.com')
        await user.type(screen.getByLabelText(/^пароль$/i), 'longenough1')
        await user.type(screen.getByLabelText(/повторите пароль/i), 'longenough1')
        await user.click(screen.getByRole('button', { name: /зарегистрироваться/i }))

        expect(await screen.findByRole('alert')).toHaveTextContent('Username already taken')
        expect(errorToasts()).toHaveLength(0)
    })
})
