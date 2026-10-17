import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import CreateProjectRoleModal from './CreateProjectRoleModal'
import MemberRolesModal from './MemberRolesModal'
import PermissionPickerLocal from './PermissionPickerLocal'
import ProjectRolesSection from './ProjectRolesSection'

vi.mock('../../api/permissions', () => ({
  permissionsApi: {
    listPermissions: vi.fn(), listProjectRoles: vi.fn(), createProjectRole: vi.fn(),
    grantProjectRole: vi.fn(), revokeProjectRole: vi.fn(), deleteProjectRole: vi.fn(),
  },
}))
import { permissionsApi } from '../../api/permissions'

vi.mock('../PermissionGate', () => ({
  default: ({ children }: { children: React.ReactNode }) => <>{children}</>,
}))

const perms = [
  { id: 'p1', name: 'runs.read', scope: 'project', description: 'читать' },
  { id: 'p2', name: 'runs.write', scope: 'project', description: '' },
  { id: 's1', name: 'users.manage', scope: 'system', description: '' },
]
const roles = [
  { id: 'r1', name: 'owner', description: 'главный', is_builtin: true },
  { id: 'r2', name: 'reviewer', description: '', is_builtin: false },
]

function wrap(ui: React.ReactNode) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } })
  return render(<QueryClientProvider client={qc}>{ui}</QueryClientProvider>)
}

beforeEach(() => {
  vi.clearAllMocks()
  vi.mocked(permissionsApi.listPermissions).mockResolvedValue(perms as never)
  vi.mocked(permissionsApi.listProjectRoles).mockResolvedValue(roles as never)
})

describe('PermissionPickerLocal', () => {
  it('lists only permissions of its scope and toggles them', async () => {
    const onChange = vi.fn()
    wrap(<PermissionPickerLocal scope="project" value={['p1']} onChange={onChange} />)
    expect(await screen.findByText('runs.read')).toBeInTheDocument()
    expect(screen.getByText(/— читать/)).toBeInTheDocument()
    expect(screen.queryByText('users.manage')).toBeNull()
    await userEvent.click(screen.getByLabelText(/runs.write/))
    expect(onChange).toHaveBeenLastCalledWith(['p1', 'p2'])
    await userEvent.click(screen.getByLabelText(/runs.read/))
    expect(onChange).toHaveBeenLastCalledWith([])
  })

  it('shows a placeholder when the scope is empty', async () => {
    vi.mocked(permissionsApi.listPermissions).mockResolvedValue([] as never)
    wrap(<PermissionPickerLocal scope="system" value={[]} onChange={vi.fn()} />)
    expect(await screen.findByText('Нет доступных разрешений')).toBeInTheDocument()
  })

  it('shows a spinner while loading and disables checkboxes when asked', async () => {
    wrap(<PermissionPickerLocal scope="project" value={[]} onChange={vi.fn()} disabled />)
    expect(screen.getByRole('progressbar')).toBeInTheDocument()
    expect(await screen.findByLabelText(/runs.read/)).toBeDisabled()
  })
})

describe('CreateProjectRoleModal', () => {
  it('requires a name, then creates the role with the chosen permissions', async () => {
    vi.mocked(permissionsApi.createProjectRole).mockResolvedValue({} as never)
    const onClose = vi.fn()
    wrap(<CreateProjectRoleModal isOpen onClose={onClose} projectId="pr1" />)
    expect(screen.getByText('Создать роль')).toBeDisabled()
    await userEvent.type(screen.getByLabelText(/Название/), '  ')
    await userEvent.click(screen.getByLabelText(/runs.read/))
    await userEvent.clear(screen.getByLabelText(/Название/))
    await userEvent.type(screen.getByLabelText(/Название/), 'qa')
    await userEvent.type(screen.getByLabelText('Описание'), 'tests')
    await userEvent.click(screen.getByText('Создать роль'))
    await waitFor(() => expect(onClose).toHaveBeenCalled())
    expect(permissionsApi.createProjectRole).toHaveBeenCalledWith('pr1', { name: 'qa', description: 'tests', permissions: ['p1'] })
  })

  it('shows the server error inline and cancel closes', async () => {
    vi.mocked(permissionsApi.createProjectRole).mockRejectedValue(new Error('dup'))
    const onClose = vi.fn()
    wrap(<CreateProjectRoleModal isOpen onClose={onClose} projectId="pr1" />)
    await userEvent.type(screen.getByLabelText(/Название/), 'qa')
    await userEvent.click(screen.getByText('Создать роль'))
    expect(await screen.findByRole('alert')).toHaveTextContent('Ошибка создания роли')
    expect(onClose).not.toHaveBeenCalled()
    await userEvent.click(screen.getByText('Отмена'))
    expect(onClose).toHaveBeenCalled()
  })

  it('does not fetch or render anything when closed', () => {
    wrap(<CreateProjectRoleModal isOpen={false} onClose={vi.fn()} projectId="pr1" />)
    expect(screen.queryByText('Создать роль проекта')).toBeNull()
  })
})

describe('MemberRolesModal', () => {
  const props = { isOpen: true, onClose: vi.fn(), projectId: 'pr1', userId: 'u1', username: 'alice' }

  it('grants and revokes roles depending on the assignment', async () => {
    vi.mocked(permissionsApi.grantProjectRole).mockResolvedValue({} as never)
    vi.mocked(permissionsApi.revokeProjectRole).mockResolvedValue({} as never)
    wrap(<MemberRolesModal {...props} assignedRoleIds={['r1']} />)
    expect(await screen.findByText('Роли участника: alice')).toBeInTheDocument()
    expect(await screen.findByText('Встроенная')).toBeInTheDocument()
    await userEvent.click(screen.getByText('Отозвать'))
    await waitFor(() => expect(permissionsApi.revokeProjectRole).toHaveBeenCalledWith('pr1', 'u1', 'r1'))
    await userEvent.click(screen.getByText('Назначить'))
    await waitFor(() => expect(permissionsApi.grantProjectRole).toHaveBeenCalledWith('pr1', 'u1', 'r2'))
    await userEvent.click(screen.getByText('Закрыть'))
    expect(props.onClose).toHaveBeenCalled()
  })

  it('shows the empty state', async () => {
    vi.mocked(permissionsApi.listProjectRoles).mockResolvedValue([] as never)
    wrap(<MemberRolesModal {...props} assignedRoleIds={[]} />)
    expect(await screen.findByText('Нет доступных ролей')).toBeInTheDocument()
  })
})

describe('ProjectRolesSection', () => {
  it('expands, lists roles and offers deletion only for custom ones', async () => {
    vi.mocked(permissionsApi.deleteProjectRole).mockResolvedValue({} as never)
    const confirmSpy = vi.spyOn(window, 'confirm').mockReturnValueOnce(false).mockReturnValueOnce(true)
    wrap(<ProjectRolesSection projectId="pr1" />)
    expect(screen.queryByText('owner')).toBeNull()
    await userEvent.click(screen.getByText('Роли проекта'))
    expect(await screen.findByText('owner')).toBeInTheDocument()
    expect(screen.getByText('Кастомная')).toBeInTheDocument()
    expect(screen.getAllByText('Удалить')).toHaveLength(1)
    await userEvent.click(screen.getByText('Удалить'))
    expect(permissionsApi.deleteProjectRole).not.toHaveBeenCalled()
    await userEvent.click(screen.getByText('Удалить'))
    await waitFor(() => expect(permissionsApi.deleteProjectRole).toHaveBeenCalledWith('pr1', 'r2'))
    confirmSpy.mockRestore()
  })

  it('shows the empty message, opens the create modal and collapses', async () => {
    vi.mocked(permissionsApi.listProjectRoles).mockResolvedValue([] as never)
    wrap(<ProjectRolesSection projectId="pr1" />)
    await userEvent.click(screen.getByText('Роли проекта'))
    expect(await screen.findByText('Нет ролей')).toBeInTheDocument()
    await userEvent.click(screen.getByText('+ Создать роль'))
    expect(await screen.findByText('Создать роль проекта')).toBeInTheDocument()
    await userEvent.click(screen.getByText('Отмена'))
    await userEvent.click(screen.getByText('Роли проекта'))
    expect(screen.queryByText('Нет ролей')).toBeNull()
  })
})
