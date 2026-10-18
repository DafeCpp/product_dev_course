import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor, within, act } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import AdminUsers from './AdminUsers'

vi.mock('../api/auth', () => ({
  authApi: {
    adminListUsers: vi.fn(), adminUpdateUser: vi.fn(), adminDeleteUser: vi.fn(), adminResetUserPassword: vi.fn(),
    adminListInvites: vi.fn(), adminCreateInvite: vi.fn(), adminRevokeInvite: vi.fn(),
  },
}))
vi.mock('../api/permissions', () => ({
  permissionsApi: { listSystemRoles: vi.fn(), createSystemRole: vi.fn(), deleteSystemRole: vi.fn() },
}))
vi.mock('../components/PermissionGate', () => ({
  default: ({ children }: { children: React.ReactNode }) => <>{children}</>,
}))
vi.mock('../components/UserRolesModal', () => ({
  default: ({ username, onClose }: { username: string; onClose: () => void }) => (
    <div data-testid="roles-modal">roles of {username}<button onClick={onClose}>close-roles</button></div>
  ),
}))
const notifySuccess = vi.fn(), notifyError = vi.fn()
vi.mock('../utils/notify', () => ({ notifySuccess: (m: string) => notifySuccess(m), notifyError: (m: string) => notifyError(m) }))

import { authApi } from '../api/auth'
import { permissionsApi } from '../api/permissions'

const user = (over: Record<string, unknown> = {}) => ({
  id: 'u1', username: 'alice', email: 'alice@example.com', is_active: true, is_admin: false,
  password_change_required: false, system_roles: [], created_at: '2026-01-02T10:00:00Z', ...over,
})
const invite = (over: Record<string, unknown> = {}) => ({
  id: 'i1', token: 'abcdefgh12345678', email_hint: 'bob@example.com', is_active: true,
  expires_at: '2026-02-01T10:00:00Z', used_at: null, ...over,
})

function renderPage() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } })
  return render(<QueryClientProvider client={qc}><AdminUsers /></QueryClientProvider>)
}

describe('AdminUsers', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    vi.mocked(authApi.adminListUsers).mockResolvedValue([user(), user({ id: 'u2', username: 'bob', email: 'b@x.y', is_active: false, is_admin: true, password_change_required: true, system_roles: ['auditor'] })] as never)
    vi.mocked(authApi.adminListInvites).mockResolvedValue([invite(), invite({ id: 'i2', token: 'zzzzzzzz00', is_active: false, used_at: '2026-01-05T10:00:00Z', email_hint: null })] as never)
    vi.mocked(permissionsApi.listSystemRoles).mockResolvedValue([
      { id: 'r1', name: 'admin', description: 'all', is_builtin: true },
      { id: 'r2', name: 'ops', description: null, is_builtin: false },
    ] as never)
  })
  afterEach(() => vi.restoreAllMocks())

  describe('users tab', () => {
    it('lists users with status, roles and badges', async () => {
      renderPage()
      expect(screen.getByText('Загрузка пользователей...')).toBeInTheDocument()
      expect(await screen.findByText('alice')).toBeInTheDocument()
      expect(screen.getByText('Активен')).toBeInTheDocument()
      expect(screen.getByText('Деактивирован')).toBeInTheDocument()
      expect(screen.getByText('auditor')).toBeInTheDocument()
      expect(screen.getByText('user')).toBeInTheDocument()
      expect(screen.getByText('pwd')).toBeInTheDocument()
      expect(screen.getAllByText('02.01.2026 10:00')).toHaveLength(2)
    })

    it('shows empty and error states', async () => {
      vi.mocked(authApi.adminListUsers).mockResolvedValueOnce([])
      const { unmount } = renderPage()
      expect(await screen.findByText('Пользователей не найдено')).toBeInTheDocument()
      unmount()
      vi.mocked(authApi.adminListUsers).mockRejectedValue(new Error('нет доступа'))
      renderPage()
      expect(await screen.findByText('нет доступа', {}, { timeout: 5000 })).toBeInTheDocument()
    })

    it('debounces the search and applies the status filter', async () => {
      renderPage()
      await screen.findByText('alice')
      await userEvent.type(screen.getByPlaceholderText(/Поиск/), 'bo')
      await waitFor(() => expect(authApi.adminListUsers).toHaveBeenLastCalledWith({ search: 'bo', is_active: undefined }))
      await userEvent.selectOptions(screen.getByRole('combobox'), 'inactive')
      await waitFor(() => expect(authApi.adminListUsers).toHaveBeenLastCalledWith({ search: 'bo', is_active: false }))
      await userEvent.selectOptions(screen.getByRole('combobox'), 'active')
      await waitFor(() => expect(authApi.adminListUsers).toHaveBeenLastCalledWith({ search: 'bo', is_active: true }))
    })

    it('toggles activity and admin rights', async () => {
      vi.mocked(authApi.adminUpdateUser).mockResolvedValue({} as never)
      renderPage()
      await screen.findByText('alice')
      await userEvent.click(screen.getByTitle('Деактивировать'))
      await waitFor(() => expect(authApi.adminUpdateUser).toHaveBeenCalledWith('u1', { is_active: false }))
      await userEvent.click(screen.getByTitle('Активировать'))
      await waitFor(() => expect(authApi.adminUpdateUser).toHaveBeenCalledWith('u2', { is_active: true }))
      await userEvent.click(screen.getByTitle('Сделать admin'))
      await waitFor(() => expect(authApi.adminUpdateUser).toHaveBeenCalledWith('u1', { is_admin: true }))
      await userEvent.click(screen.getByTitle('Убрать права admin'))
      await waitFor(() => expect(authApi.adminUpdateUser).toHaveBeenCalledWith('u2', { is_admin: false }))
    })

    it('opens and closes the roles modal', async () => {
      renderPage()
      await screen.findByText('alice')
      await userEvent.click(screen.getAllByTitle('Управление ролями')[0])
      expect(screen.getByTestId('roles-modal')).toHaveTextContent('roles of alice')
      await userEvent.click(screen.getByText('close-roles'))
      expect(screen.queryByTestId('roles-modal')).toBeNull()
    })

    it('resets a password with a generated temp password that can be copied', async () => {
      const writeText = vi.fn().mockResolvedValue(undefined)
      Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true })
      vi.mocked(authApi.adminResetUserPassword).mockImplementation(async (id) => ({ user: { id }, password_change_required: true }) as never)
      renderPage()
      await screen.findByText('alice')
      await userEvent.click(screen.getAllByTitle('Сбросить пароль')[0])
      const result = await screen.findByTitle('Нажмите чтобы скопировать')
      const pw = vi.mocked(authApi.adminResetUserPassword).mock.calls[0][1] as string
      expect(pw).toMatch(/^Tmp1[A-Za-z2-9]{16}$/)
      expect(result).toHaveTextContent(pw)
      await userEvent.click(result)
      await waitFor(() => expect(notifySuccess).toHaveBeenCalledWith('Пароль скопирован'))
      expect(writeText).toHaveBeenCalledWith(pw)
    })

    it('reports a copy failure for the reset password', async () => {
      Object.defineProperty(navigator, 'clipboard', { value: { writeText: vi.fn().mockRejectedValue(new Error('x')) }, configurable: true })
      Object.defineProperty(document, 'execCommand', { value: vi.fn(() => { throw new Error('y') }), configurable: true })
      vi.mocked(authApi.adminResetUserPassword).mockImplementation(async (id) => ({ user: { id }, password_change_required: true }) as never)
      renderPage()
      await screen.findByText('alice')
      await userEvent.click(screen.getAllByTitle('Сбросить пароль')[0])
      await userEvent.click(await screen.findByTitle('Нажмите чтобы скопировать'))
      await waitFor(() => expect(notifyError).toHaveBeenCalledWith('Не удалось скопировать пароль'))
    })

    it('deletes a user only after confirmation', async () => {
      vi.mocked(authApi.adminDeleteUser).mockResolvedValue(undefined)
      const confirmSpy = vi.spyOn(window, 'confirm').mockReturnValueOnce(false).mockReturnValueOnce(true)
      renderPage()
      await screen.findByText('alice')
      const del = screen.getAllByTitle('Удалить пользователя')[0]
      await userEvent.click(del)
      expect(authApi.adminDeleteUser).not.toHaveBeenCalled()
      await userEvent.click(del)
      await waitFor(() => expect(authApi.adminDeleteUser).toHaveBeenCalledWith('u1'))
      expect(confirmSpy).toHaveBeenCalledWith('Удалить пользователя alice?')
    })
  })

  describe('system roles tab', () => {
    const open = async () => {
      renderPage()
      await userEvent.click(screen.getByText('Системные роли', { selector: 'button' }))
    }

    it('lists roles, builtin ones are not deletable', async () => {
      await open()
      expect(await screen.findByText('ops')).toBeInTheDocument()
      expect(screen.getByText('встроенная')).toBeInTheDocument()
      expect(screen.getByText('all')).toBeInTheDocument()
      expect(screen.getAllByText('Удалить')).toHaveLength(1)
    })

    it('shows empty and error states', async () => {
      vi.mocked(permissionsApi.listSystemRoles).mockResolvedValueOnce([])
      await open()
      expect(await screen.findByText('Системных ролей нет')).toBeInTheDocument()
    })

    it('shows a load error', async () => {
      vi.mocked(permissionsApi.listSystemRoles).mockRejectedValue(new Error('боль'))
      await open()
      expect(await screen.findByText('боль')).toBeInTheDocument()
    })

    it('creates a role and resets the form', async () => {
      vi.mocked(permissionsApi.createSystemRole).mockResolvedValue({} as never)
      await open()
      await screen.findByText('ops')
      await userEvent.click(screen.getByText('Создать роль'))
      const submit = screen.getByRole('button', { name: 'Создать' })
      expect(submit).toBeDisabled()
      await userEvent.type(screen.getByLabelText(/Название/), ' qa ')
      await userEvent.type(screen.getByLabelText(/Описание/), ' tests ')
      await userEvent.click(submit)
      await waitFor(() => expect(permissionsApi.createSystemRole).toHaveBeenCalledWith({ name: 'qa', description: 'tests', permissions: [] }))
      await waitFor(() => expect(screen.queryByText('Новая системная роль')).toBeNull())
    })

    it('cancels role creation', async () => {
      await open()
      await screen.findByText('ops')
      await userEvent.click(screen.getByText('Создать роль'))
      await userEvent.click(screen.getAllByText('Отмена')[1])
      expect(screen.queryByText('Новая системная роль')).toBeNull()
    })

    it('deletes a custom role after confirmation', async () => {
      vi.mocked(permissionsApi.deleteSystemRole).mockResolvedValue(undefined as never)
      const confirmSpy = vi.spyOn(window, 'confirm').mockReturnValueOnce(false).mockReturnValueOnce(true)
      await open()
      await screen.findByText('ops')
      await userEvent.click(screen.getByText('Удалить'))
      expect(permissionsApi.deleteSystemRole).not.toHaveBeenCalled()
      await userEvent.click(screen.getByText('Удалить'))
      await waitFor(() => expect(permissionsApi.deleteSystemRole).toHaveBeenCalledWith('r2'))
      expect(confirmSpy).toHaveBeenLastCalledWith('Удалить роль «ops»?')
    })
  })

  describe('invites tab', () => {
    const open = async () => {
      renderPage()
      await userEvent.click(screen.getByText('Инвайты', { selector: 'button' }))
    }

    it('lists invites and only offers revoke for active ones', async () => {
      await open()
      expect(await screen.findByText('abcdefgh…')).toBeInTheDocument()
      expect(screen.getByText('Активен')).toBeInTheDocument()
      expect(screen.getByText('Использован/истёк')).toBeInTheDocument()
      expect(screen.getByText('05.01.2026 10:00')).toBeInTheDocument()
      expect(screen.getAllByText('Отозвать')).toHaveLength(1)
    })

    it('toggles the active-only filter', async () => {
      await open()
      await screen.findByText('abcdefgh…')
      expect(authApi.adminListInvites).toHaveBeenLastCalledWith(true)
      await userEvent.click(screen.getByLabelText('Только активные'))
      await waitFor(() => expect(authApi.adminListInvites).toHaveBeenLastCalledWith(false))
    })

    it('shows the empty state with a shortcut to create the first invite', async () => {
      vi.mocked(authApi.adminListInvites).mockResolvedValue([])
      await open()
      await userEvent.click(await screen.findByText('Создать первый инвайт'))
      expect(screen.getByLabelText(/Срок действия/)).toBeInTheDocument()
      expect(screen.queryByText('Создать первый инвайт')).toBeNull()
    })

    it('shows a load error', async () => {
      vi.mocked(authApi.adminListInvites).mockRejectedValue(new Error('инвайты упали'))
      await open()
      expect(await screen.findByText('инвайты упали', {}, { timeout: 5000 })).toBeInTheDocument()
    })

    it('creates an invite with defaults and with custom values', async () => {
      vi.mocked(authApi.adminCreateInvite).mockResolvedValue({} as never)
      await open()
      await screen.findByText('abcdefgh…')
      await userEvent.click(screen.getByText('Создать инвайт'))
      await userEvent.click(screen.getByRole('button', { name: 'Создать' }))
      await waitFor(() => expect(authApi.adminCreateInvite).toHaveBeenLastCalledWith({ email_hint: undefined, expires_in_hours: 72 }))
      await userEvent.click(screen.getByText('Создать инвайт'))
      await userEvent.type(screen.getByLabelText(/Email/), ' a@b.c ')
      await userEvent.clear(screen.getByLabelText(/Срок действия/))
      await userEvent.type(screen.getByLabelText(/Срок действия/), '5')
      await userEvent.click(screen.getByRole('button', { name: 'Создать' }))
      await waitFor(() => expect(authApi.adminCreateInvite).toHaveBeenLastCalledWith({ email_hint: 'a@b.c', expires_in_hours: 5 }))
    })

    it('cancels invite creation', async () => {
      await open()
      await screen.findByText('abcdefgh…')
      await userEvent.click(screen.getByText('Создать инвайт'))
      await userEvent.click(within(document.querySelector('.invite-create-form') as HTMLElement).getByText('Отмена'))
      expect(screen.queryByLabelText(/Срок действия/)).toBeNull()
    })

    it('revokes an invite after confirmation', async () => {
      vi.mocked(authApi.adminRevokeInvite).mockResolvedValue(undefined)
      vi.spyOn(window, 'confirm').mockReturnValueOnce(false).mockReturnValueOnce(true)
      await open()
      await screen.findByText('abcdefgh…')
      await userEvent.click(screen.getByText('Отозвать'))
      expect(authApi.adminRevokeInvite).not.toHaveBeenCalled()
      await userEvent.click(screen.getByText('Отозвать'))
      await waitFor(() => expect(authApi.adminRevokeInvite).toHaveBeenCalledWith('abcdefgh12345678'))
    })

    it('copies the token and the registration link with a transient checkmark', async () => {
      const writeText = vi.fn().mockResolvedValue(undefined)
      Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true })
      await open()
      await screen.findByText('abcdefgh…')
      await userEvent.click(screen.getAllByTitle('Копировать токен')[0])
      expect(writeText).toHaveBeenLastCalledWith('abcdefgh12345678')
      expect(await screen.findByText('✓')).toBeInTheDocument()
      await userEvent.click(screen.getAllByTitle('Скопировать ссылку для регистрации')[0])
      expect(writeText).toHaveBeenLastCalledWith(`${window.location.origin}/register?token=abcdefgh12345678`)
      await waitFor(() => expect(screen.getAllByText('✓')).toHaveLength(1))
    })

    it('falls back to execCommand and reports failures', async () => {
      Object.defineProperty(navigator, 'clipboard', { value: undefined, configurable: true })
      const exec = vi.fn().mockReturnValue(true)
      Object.defineProperty(document, 'execCommand', { value: exec, configurable: true })
      await open()
      await screen.findByText('abcdefgh…')
      await userEvent.click(screen.getAllByTitle('Копировать токен')[0])
      await waitFor(() => expect(exec).toHaveBeenCalledWith('copy'))
      exec.mockImplementation(() => { throw new Error('nope') })
      await userEvent.click(screen.getAllByTitle('Копировать токен')[0])
      await waitFor(() => expect(notifyError).toHaveBeenCalledWith('Не удалось скопировать токен'))
      await userEvent.click(screen.getAllByTitle('Скопировать ссылку для регистрации')[0])
      await waitFor(() => expect(notifyError).toHaveBeenCalledWith('Не удалось скопировать ссылку'))
      await act(async () => { await Promise.resolve() })
    })
  })
})
