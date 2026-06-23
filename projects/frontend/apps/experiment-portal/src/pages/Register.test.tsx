import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import Register from './Register'
import { authApi } from '../api/auth'

vi.mock('../api/auth', () => ({
  authApi: {
    register: vi.fn(),
  },
}))

const mockNavigate = vi.fn()
let searchParamsString = ''
vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual<typeof import('react-router-dom')>('react-router-dom')
  return {
    ...actual,
    useNavigate: () => mockNavigate,
    useSearchParams: () => [new URLSearchParams(searchParamsString), vi.fn()],
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

describe('Register', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    mockNavigate.mockClear()
    searchParamsString = ''
  })

  it('renders the registration form', () => {
    render(<Register />, { wrapper: createWrapper() })

    expect(screen.getByRole('heading', { name: /регистрация/i })).toBeInTheDocument()
    expect(screen.getByLabelText(/имя пользователя/i)).toBeInTheDocument()
    expect(screen.getByLabelText(/^email$/i)).toBeInTheDocument()
    expect(screen.getByLabelText(/^пароль$/i)).toBeInTheDocument()
    expect(screen.getByLabelText(/повторите пароль/i)).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /зарегистрироваться/i })).toBeInTheDocument()
  })

  it('pre-fills email from email_hint search param and shows invite banner', () => {
    searchParamsString = 'token=abc&email_hint=alice%40example.com'
    render(<Register />, { wrapper: createWrapper() })

    const emailInput = screen.getByLabelText(/^email$/i) as HTMLInputElement
    expect(emailInput.value).toBe('alice@example.com')
    expect(screen.getByText(/регистрация по приглашению/i)).toBeInTheDocument()
    expect(screen.getByText(/alice@example\.com/i)).toBeInTheDocument()
  })

  it('rejects short username before calling the API', async () => {
    const user = userEvent.setup()
    const mockRegister = vi.mocked(authApi.register)
    render(<Register />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/имя пользователя/i), 'ab')
    await user.type(screen.getByLabelText(/^email$/i), 'a@b.co')
    await user.type(screen.getByLabelText(/^пароль$/i), 'longenough')
    await user.type(screen.getByLabelText(/повторите пароль/i), 'longenough')
    await user.click(screen.getByRole('button', { name: /зарегистрироваться/i }))

    await waitFor(() => {
      expect(screen.getByText(/имя пользователя должно быть не короче 3/i)).toBeInTheDocument()
    })
    expect(mockRegister).not.toHaveBeenCalled()
  })

  it('rejects invalid email format', async () => {
    const mockRegister = vi.mocked(authApi.register)
    render(<Register />, { wrapper: createWrapper() })

    // Set field values directly and submit the form, bypassing HTML5 validation
    // (which would otherwise short-circuit the click handler before our regex check).
    fireEvent.change(screen.getByLabelText(/имя пользователя/i), { target: { value: 'alice' } })
    fireEvent.change(screen.getByLabelText(/^email$/i), { target: { value: 'not-an-email' } })
    fireEvent.change(screen.getByLabelText(/^пароль$/i), { target: { value: 'longenough' } })
    fireEvent.change(screen.getByLabelText(/повторите пароль/i), { target: { value: 'longenough' } })

    const form = screen.getByRole('button', { name: /зарегистрироваться/i }).closest('form')!
    fireEvent.submit(form)

    await waitFor(() => {
      expect(screen.getByText(/введите корректный email/i)).toBeInTheDocument()
    })
    expect(mockRegister).not.toHaveBeenCalled()
  })

  it('rejects short password', async () => {
    const user = userEvent.setup()
    const mockRegister = vi.mocked(authApi.register)
    render(<Register />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/имя пользователя/i), 'alice')
    await user.type(screen.getByLabelText(/^email$/i), 'a@b.co')
    await user.type(screen.getByLabelText(/^пароль$/i), 'short')
    await user.type(screen.getByLabelText(/повторите пароль/i), 'short')
    await user.click(screen.getByRole('button', { name: /зарегистрироваться/i }))

    await waitFor(() => {
      expect(screen.getByText(/пароль должен быть не короче 8/i)).toBeInTheDocument()
    })
    expect(mockRegister).not.toHaveBeenCalled()
  })

  it('rejects mismatched password confirmation', async () => {
    const user = userEvent.setup()
    const mockRegister = vi.mocked(authApi.register)
    render(<Register />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/имя пользователя/i), 'alice')
    await user.type(screen.getByLabelText(/^email$/i), 'a@b.co')
    await user.type(screen.getByLabelText(/^пароль$/i), 'longenough')
    await user.type(screen.getByLabelText(/повторите пароль/i), 'differentpass')
    await user.click(screen.getByRole('button', { name: /зарегистрироваться/i }))

    await waitFor(() => {
      expect(screen.getByText(/пароли не совпадают/i)).toBeInTheDocument()
    })
    expect(mockRegister).not.toHaveBeenCalled()
  })

  it('submits valid form and redirects to login on success', async () => {
    const user = userEvent.setup()
    const mockRegister = vi.mocked(authApi.register)
    mockRegister.mockResolvedValueOnce({} as any)

    render(<Register />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/имя пользователя/i), 'alice')
    await user.type(screen.getByLabelText(/^email$/i), 'alice@example.com')
    await user.type(screen.getByLabelText(/^пароль$/i), 'longenough')
    await user.type(screen.getByLabelText(/повторите пароль/i), 'longenough')
    await user.click(screen.getByRole('button', { name: /зарегистрироваться/i }))

    await waitFor(() => {
      expect(mockRegister).toHaveBeenCalledWith({
        username: 'alice',
        email: 'alice@example.com',
        password: 'longenough',
      })
      expect(mockNavigate).toHaveBeenCalledWith('/login')
    })
  })

  it('passes invite_token from URL when present', async () => {
    searchParamsString = 'token=invite-xyz'
    const user = userEvent.setup()
    const mockRegister = vi.mocked(authApi.register)
    mockRegister.mockResolvedValueOnce({} as any)

    render(<Register />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/имя пользователя/i), 'alice')
    await user.type(screen.getByLabelText(/^email$/i), 'alice@example.com')
    await user.type(screen.getByLabelText(/^пароль$/i), 'longenough')
    await user.type(screen.getByLabelText(/повторите пароль/i), 'longenough')
    await user.click(screen.getByRole('button', { name: /зарегистрироваться/i }))

    await waitFor(() => {
      expect(mockRegister).toHaveBeenCalledWith(
        expect.objectContaining({ invite_token: 'invite-xyz' })
      )
    })
  })

  it('renders API error message on failure', async () => {
    const user = userEvent.setup()
    const mockRegister = vi.mocked(authApi.register)
    mockRegister.mockRejectedValueOnce({
      response: { data: { error: 'Username already exists' } },
    })

    render(<Register />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/имя пользователя/i), 'alice')
    await user.type(screen.getByLabelText(/^email$/i), 'alice@example.com')
    await user.type(screen.getByLabelText(/^пароль$/i), 'longenough')
    await user.type(screen.getByLabelText(/повторите пароль/i), 'longenough')
    await user.click(screen.getByRole('button', { name: /зарегистрироваться/i }))

    await waitFor(() => {
      expect(screen.getByText(/username already exists/i)).toBeInTheDocument()
    })
    expect(mockNavigate).not.toHaveBeenCalled()
  })
})
