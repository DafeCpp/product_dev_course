import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ChangePassword from './ChangePassword'
import { authApi } from '../api/auth'

vi.mock('../api/auth', () => ({
  authApi: {
    changePassword: vi.fn(),
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
  const qc = new QueryClient({
    defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
  })
  return ({ children }: { children: React.ReactNode }) => (
    <QueryClientProvider client={qc}>
      <MemoryRouter>{children}</MemoryRouter>
    </QueryClientProvider>
  )
}

describe('ChangePassword', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    mockNavigate.mockClear()
  })

  it('renders the change password form', () => {
    render(<ChangePassword />, { wrapper: createWrapper() })

    expect(screen.getByRole('heading', { name: /смена пароля/i })).toBeInTheDocument()
    expect(screen.getByLabelText(/текущий пароль/i)).toBeInTheDocument()
    expect(screen.getByLabelText(/^новый пароль$/i)).toBeInTheDocument()
    expect(screen.getByLabelText(/подтверждение нового пароля/i)).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /сменить пароль/i })).toBeInTheDocument()
  })

  it('rejects mismatched new and confirmation password', async () => {
    const user = userEvent.setup()
    const mockChange = vi.mocked(authApi.changePassword)
    render(<ChangePassword />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/текущий пароль/i), 'currentpw')
    await user.type(screen.getByLabelText(/^новый пароль$/i), 'longenough')
    await user.type(screen.getByLabelText(/подтверждение нового пароля/i), 'differentone')
    await user.click(screen.getByRole('button', { name: /сменить пароль/i }))

    await waitFor(() => {
      expect(screen.getByText(/пароли не совпадают/i)).toBeInTheDocument()
    })
    expect(mockChange).not.toHaveBeenCalled()
  })

  it('rejects new password shorter than 8 characters', async () => {
    const user = userEvent.setup()
    const mockChange = vi.mocked(authApi.changePassword)
    render(<ChangePassword />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/текущий пароль/i), 'currentpw')
    await user.type(screen.getByLabelText(/^новый пароль$/i), 'short')
    await user.type(screen.getByLabelText(/подтверждение нового пароля/i), 'short')
    await user.click(screen.getByRole('button', { name: /сменить пароль/i }))

    await waitFor(() => {
      expect(
        screen.getByText(/новый пароль должен содержать не менее 8 символов/i)
      ).toBeInTheDocument()
    })
    expect(mockChange).not.toHaveBeenCalled()
  })

  it('submits change password and redirects to /experiments on success', async () => {
    const user = userEvent.setup()
    const mockChange = vi.mocked(authApi.changePassword)
    mockChange.mockResolvedValueOnce(undefined as any)

    render(<ChangePassword />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/текущий пароль/i), 'currentpw')
    await user.type(screen.getByLabelText(/^новый пароль$/i), 'longenough')
    await user.type(screen.getByLabelText(/подтверждение нового пароля/i), 'longenough')
    await user.click(screen.getByRole('button', { name: /сменить пароль/i }))

    await waitFor(() => {
      expect(mockChange).toHaveBeenCalledWith({
        old_password: 'currentpw',
        new_password: 'longenough',
      })
      expect(mockNavigate).toHaveBeenCalledWith('/experiments', { replace: true })
    })
  })

  it('renders API error message on failure', async () => {
    const user = userEvent.setup()
    const mockChange = vi.mocked(authApi.changePassword)
    mockChange.mockRejectedValueOnce({
      response: { data: { error: 'Wrong current password' } },
    })

    render(<ChangePassword />, { wrapper: createWrapper() })

    await user.type(screen.getByLabelText(/текущий пароль/i), 'badpw')
    await user.type(screen.getByLabelText(/^новый пароль$/i), 'longenough')
    await user.type(screen.getByLabelText(/подтверждение нового пароля/i), 'longenough')
    await user.click(screen.getByRole('button', { name: /сменить пароль/i }))

    await waitFor(() => {
      expect(screen.getByText(/wrong current password/i)).toBeInTheDocument()
    })
    expect(mockNavigate).not.toHaveBeenCalled()
  })
})
