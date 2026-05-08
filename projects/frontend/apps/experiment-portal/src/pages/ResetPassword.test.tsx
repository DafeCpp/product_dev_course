import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ResetPassword from './ResetPassword'
import { authApi } from '../api/auth'

vi.mock('../api/auth', () => ({
  authApi: {
    confirmPasswordReset: vi.fn(),
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
  notifySuccess: vi.fn(),
}))

const createWrapper = () => {
  const qc = new QueryClient({
    defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
  })
  return ({ children }: { children: React.ReactNode }) => (
    <QueryClientProvider client={qc}>
      <MemoryRouter>{children}</MemoryRouter>
    </QueryClientProvider>
  )
}

describe('ResetPassword', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    mockNavigate.mockClear()
    searchParamsString = 'token=reset-token-abc'
  })

  it('renders error UI when token is missing from URL', () => {
    searchParamsString = ''
    render(<ResetPassword />, { wrapper: createWrapper() })

    expect(screen.getByText(/недействительная ссылка для сброса пароля/i)).toBeInTheDocument()
    // No password fields are rendered in the error state.
    expect(screen.queryByLabelText(/новый пароль/i)).not.toBeInTheDocument()
  })

  it('renders the reset form when token is present', () => {
    render(<ResetPassword />, { wrapper: createWrapper() })

    expect(screen.getByRole('heading', { name: /новый пароль/i })).toBeInTheDocument()
    expect(screen.getByLabelText(/^новый пароль$/i)).toBeInTheDocument()
    expect(screen.getByLabelText(/подтвердите пароль/i)).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /установить новый пароль/i })).toBeInTheDocument()
  })

  it('rejects passwords shorter than 8 characters', async () => {
    const user = userEvent.setup()
    const mockReset = vi.mocked(authApi.confirmPasswordReset)
    render(<ResetPassword />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/^новый пароль$/i), 'short')
    await user.type(screen.getByLabelText(/подтвердите пароль/i), 'short')
    await user.click(screen.getByRole('button', { name: /установить новый пароль/i }))

    await waitFor(() => {
      expect(screen.getByText(/не менее 8 символов/i)).toBeInTheDocument()
    })
    expect(mockReset).not.toHaveBeenCalled()
  })

  it('rejects mismatched passwords', async () => {
    const user = userEvent.setup()
    const mockReset = vi.mocked(authApi.confirmPasswordReset)
    render(<ResetPassword />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/^новый пароль$/i), 'longenough')
    await user.type(screen.getByLabelText(/подтвердите пароль/i), 'differentone')
    await user.click(screen.getByRole('button', { name: /установить новый пароль/i }))

    await waitFor(() => {
      expect(screen.getByText(/пароли не совпадают/i)).toBeInTheDocument()
    })
    expect(mockReset).not.toHaveBeenCalled()
  })

  it('submits new password and redirects to login on success', async () => {
    const user = userEvent.setup()
    const mockReset = vi.mocked(authApi.confirmPasswordReset)
    mockReset.mockResolvedValueOnce(undefined as any)

    render(<ResetPassword />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/^новый пароль$/i), 'longenough')
    await user.type(screen.getByLabelText(/подтвердите пароль/i), 'longenough')
    await user.click(screen.getByRole('button', { name: /установить новый пароль/i }))

    await waitFor(() => {
      expect(mockReset).toHaveBeenCalledWith('reset-token-abc', 'longenough')
      expect(mockNavigate).toHaveBeenCalledWith('/login', { replace: true })
    })
  })

  it('renders API error message on failure', async () => {
    const user = userEvent.setup()
    const mockReset = vi.mocked(authApi.confirmPasswordReset)
    mockReset.mockRejectedValueOnce({
      response: { data: { error: 'Token expired' } },
    })

    render(<ResetPassword />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/^новый пароль$/i), 'longenough')
    await user.type(screen.getByLabelText(/подтвердите пароль/i), 'longenough')
    await user.click(screen.getByRole('button', { name: /установить новый пароль/i }))

    await waitFor(() => {
      expect(screen.getByText(/token expired/i)).toBeInTheDocument()
    })
    expect(mockNavigate).not.toHaveBeenCalled()
  })
})
