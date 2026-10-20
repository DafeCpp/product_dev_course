import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor, act } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import AdminUsers from './AdminUsers'

vi.mock('../api/auth', () => ({
  authApi: {
    adminListUsers: vi.fn(),
    adminUpdateUser: vi.fn(),
    adminDeleteUser: vi.fn(),
    adminResetUserPassword: vi.fn(),
    adminListInvites: vi.fn(),
    adminCreateInvite: vi.fn(),
    adminRevokeInvite: vi.fn(),
  },
}))
vi.mock('../api/permissions', () => ({
  permissionsApi: { listSystemRoles: vi.fn(), createSystemRole: vi.fn(), deleteSystemRole: vi.fn() },
}))
vi.mock('../components/PermissionGate', () => ({
  default: ({ children }: { children: React.ReactNode }) => <>{children}</>,
}))
vi.mock('../components/UserRolesModal', () => ({
  default: () => <div data-testid="roles-modal" />,
}))
vi.mock('../utils/notify', () => ({ notifySuccess: vi.fn(), notifyError: vi.fn() }))

import { authApi } from '../api/auth'
import { permissionsApi } from '../api/permissions'

function renderPage() {
  const qc = new QueryClient({
    defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
  })
  return render(
    <QueryClientProvider client={qc}>
      <AdminUsers />
    </QueryClientProvider>,
  )
}

describe('AdminUsers — branches', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    vi.mocked(authApi.adminListUsers).mockResolvedValue([
      {
        id: 'u1',
        username: 'noroles',
        email: 'n@x.y',
        is_active: true,
        is_admin: false,
        password_change_required: false,
        created_at: '2026-01-02T10:00:00Z',
      },
    ] as never)
    vi.mocked(authApi.adminListInvites).mockResolvedValue([] as never)
    vi.mocked(permissionsApi.listSystemRoles).mockResolvedValue([] as never)
  })
  afterEach(() => vi.restoreAllMocks())

  it('renders "user" badge when system_roles is missing', async () => {
    renderPage()
    expect(await screen.findByText('noroles')).toBeInTheDocument()
    expect(screen.getByText('user')).toBeInTheDocument()
  })

  it('users: non-Error failure shows fallback message', async () => {
    vi.mocked(authApi.adminListUsers).mockRejectedValue('nope')
    renderPage()
    expect(
      await screen.findByText('Ошибка загрузки пользователей', {}, { timeout: 4000 }),
    ).toBeInTheDocument()
  })

  it('roles: non-Error failure shows fallback message', async () => {
    vi.mocked(permissionsApi.listSystemRoles).mockRejectedValue('nope')
    const user = userEvent.setup()
    renderPage()
    await user.click(screen.getByRole('button', { name: 'Системные роли' }))
    expect(await screen.findByText('Ошибка загрузки ролей', {}, { timeout: 4000 })).toBeInTheDocument()
  })

  it('invites: non-Error failure shows fallback message', async () => {
    vi.mocked(authApi.adminListInvites).mockRejectedValue('nope')
    const user = userEvent.setup()
    renderPage()
    await user.click(screen.getByRole('button', { name: 'Инвайты' }))
    expect(
      await screen.findByText('Ошибка загрузки инвайтов', {}, { timeout: 4000 }),
    ).toBeInTheDocument()
  })

  it('creates a role without description and shows pending label', async () => {
    let resolve: (v: unknown) => void = () => {}
    vi.mocked(permissionsApi.createSystemRole).mockReturnValue(
      new Promise((r) => (resolve = r)) as never,
    )
    const user = userEvent.setup()
    renderPage()
    await user.click(screen.getByRole('button', { name: 'Системные роли' }))
    await user.click(await screen.findByRole('button', { name: 'Создать роль' }))
    await user.type(screen.getByLabelText(/Название/), '  newrole  ')
    await user.type(screen.getByLabelText('Описание (необязательно)'), '   ')
    await user.click(screen.getByRole('button', { name: 'Создать' }))
    expect(await screen.findByRole('button', { name: 'Создание...' })).toBeDisabled()
    expect(permissionsApi.createSystemRole).toHaveBeenCalledWith({
      name: 'newrole',
      description: undefined,
      permissions: [],
    })
    await act(async () => {
      resolve({ id: 'r9', name: 'newrole' })
    })
  })

  it('creates an invite with invalid expiry falling back to 72h and shows pending label', async () => {
    let resolve: (v: unknown) => void = () => {}
    vi.mocked(authApi.adminCreateInvite).mockReturnValue(new Promise((r) => (resolve = r)) as never)
    const user = userEvent.setup()
    renderPage()
    await user.click(screen.getByRole('button', { name: 'Инвайты' }))
    await user.click(await screen.findByRole('button', { name: 'Создать инвайт' }))
    await user.clear(screen.getByLabelText('Срок действия (часов)'))
    await user.click(screen.getByRole('button', { name: 'Создать' }))
    expect(await screen.findByRole('button', { name: 'Создание...' })).toBeDisabled()
    expect(authApi.adminCreateInvite).toHaveBeenCalledWith({
      email_hint: undefined,
      expires_in_hours: 72,
    })
    await act(async () => {
      resolve({ id: 'i9', token: 't' })
    })
    await waitFor(() =>
      expect(screen.queryByRole('button', { name: 'Создание...' })).not.toBeInTheDocument(),
    )
  })
})
