import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor, act } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ProjectMembersModal from './ProjectMembersModal'
import { projectsApi, usersApi } from '../api/client'
import { authApi } from '../api/auth'
import { permissionsApi } from '../api/permissions'

vi.mock('../api/client', () => ({
  projectsApi: {
    listMembers: vi.fn(),
    addMember: vi.fn(),
    removeMember: vi.fn(),
    updateMemberRole: vi.fn(),
  },
  usersApi: { search: vi.fn() },
}))
vi.mock('../api/auth', () => ({ authApi: { me: vi.fn() } }))
vi.mock('../api/permissions', () => ({ permissionsApi: { listProjectRoles: vi.fn() } }))
vi.mock('../utils/notify', () => ({ notifyError: vi.fn(), notifySuccess: vi.fn() }))

vi.mock('./project-members/MembersTable', () => ({
  default: ({
    isOwner,
    onRemoveMember,
    onRoleChange,
    onManageRoles,
  }: {
    isOwner: boolean
    onRemoveMember: (id: string, name?: string | null) => void
    onRoleChange: (id: string, role: 'owner' | 'editor' | 'viewer') => void
    onManageRoles: (m: { userId: string; username: string; assignedRoleIds: string[] }) => void
  }) => (
    <div data-testid="members-table">
      <span>isOwner:{String(isOwner)}</span>
      <button onClick={() => onRemoveMember('u-x', null)}>remove-noname</button>
      <button onClick={() => onRemoveMember('u-y', 'yuser')}>remove-named</button>
      <button onClick={() => onRoleChange('u-y', 'editor')}>change-role</button>
      <button onClick={() => onManageRoles({ userId: 'u-y', username: 'yuser', assignedRoleIds: [] })}>
        manage-roles
      </button>
    </div>
  ),
}))

vi.mock('./project-members/AddMemberForm', () => ({
  default: ({
    newMemberRoleId,
    debouncedQuery,
    userOptions,
    onUserInputChange,
    onUserSelect,
    onRoleChange,
    onSubmit,
  }: {
    newMemberRoleId: string
    debouncedQuery: string
    userOptions: { id: string; username: string }[]
    onUserInputChange: (v: string) => void
    onUserSelect: (v: { id: string; username: string } | null) => void
    onRoleChange: (v: string) => void
    onSubmit: (e: React.FormEvent) => void
  }) => (
    <form data-testid="add-form" onSubmit={onSubmit}>
      <span>role:{newMemberRoleId}</span>
      <span>q:{debouncedQuery}</span>
      <span>opts:{userOptions.map((u) => u.username).join(',')}</span>
      <button type="button" onClick={() => onUserInputChange('a')}>type-1</button>
      <button type="button" onClick={() => onUserInputChange('al')}>type-2</button>
      <button type="button" onClick={() => onUserInputChange('ali')}>type-3</button>
      <button type="button" onClick={() => onUserSelect({ id: 'new-u', username: 'alice' })}>pick</button>
      <button type="button" onClick={() => onRoleChange('r-custom')}>role-custom</button>
      <button type="submit">submit-add</button>
    </form>
  ),
}))

vi.mock('./project-members/MemberRolesModal', () => ({
  default: ({ onClose, username }: { onClose: () => void; username: string }) => (
    <div data-testid="roles-modal">
      <span>roles-for:{username}</span>
      <button onClick={onClose}>close-roles</button>
    </div>
  ),
}))
vi.mock('./project-members/ProjectRolesSection', () => ({
  default: () => <div data-testid="roles-section" />,
}))

const pApi = projectsApi as unknown as Record<string, ReturnType<typeof vi.fn>>
const search = usersApi.search as unknown as ReturnType<typeof vi.fn>
const me = authApi.me as unknown as ReturnType<typeof vi.fn>
const listRoles = permissionsApi.listProjectRoles as unknown as ReturnType<typeof vi.fn>

function renderModal(ownerId = 'owner-1', onClose = vi.fn()) {
  const qc = new QueryClient({
    defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
  })
  render(
    <QueryClientProvider client={qc}>
      <ProjectMembersModal isOpen onClose={onClose} projectId="p1" projectOwnerId={ownerId} />
    </QueryClientProvider>,
  )
  return onClose
}

describe('ProjectMembersModal — branches', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    me.mockResolvedValue({ id: 'owner-1' })
    pApi.listMembers.mockResolvedValue({ members: [] })
    listRoles.mockResolvedValue([
      { id: 'r-viewer', name: 'viewer' },
      { id: 'r-custom', name: 'custom' },
    ])
    search.mockResolvedValue({ users: [{ id: 'new-u', username: 'alice' }] })
    pApi.addMember.mockResolvedValue({})
    pApi.removeMember.mockResolvedValue(undefined)
    pApi.updateMemberRole.mockResolvedValue({})
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('defaults to the first role when there is no viewer role', async () => {
    listRoles.mockResolvedValue([{ id: 'r-first', name: 'custom' }])
    renderModal()
    expect(await screen.findByText('role:r-first')).toBeInTheDocument()
  })

  it('non-owner with builtin owner role becomes owner', async () => {
    me.mockResolvedValue({ id: 'u-2' })
    pApi.listMembers.mockResolvedValue({ members: [{ user_id: 'u-2', roles: ['Owner'] }] })
    renderModal()
    expect(await screen.findByText('isOwner:true')).toBeInTheDocument()
  })

  it('non-owner member without owner role stays non-owner', async () => {
    me.mockResolvedValue({ id: 'u-2' })
    pApi.listMembers.mockResolvedValue({ members: [{ user_id: 'u-2', roles: ['editor'] }] })
    renderModal()
    expect(await screen.findByText('isOwner:false')).toBeInTheDocument()
    expect(screen.queryByTestId('add-form')).not.toBeInTheDocument()
  })

  it('non-owner who is not a member stays non-owner', async () => {
    me.mockResolvedValue({ id: 'u-3' })
    pApi.listMembers.mockResolvedValue({ members: [{ user_id: 'u-2', roles: ['owner'] }] })
    renderModal()
    expect(await screen.findByText('isOwner:false')).toBeInTheDocument()
  })

  it('requires a selected user, and maps a custom role to legacy viewer', async () => {
    const user = userEvent.setup()
    renderModal()
    await screen.findByTestId('add-form')
    await user.click(screen.getByText('submit-add'))
    expect(await screen.findByRole('alert')).toHaveTextContent('Выберите пользователя из списка')

    await user.click(screen.getByText('type-1'))
    expect(screen.getByText('q:')).toBeInTheDocument()
    await user.click(screen.getByText('type-2'))
    await user.click(screen.getByText('type-3'))
    expect(await screen.findByText('q:ali')).toBeInTheDocument()
    expect(await screen.findByText('opts:alice')).toBeInTheDocument()
    expect(search).toHaveBeenCalledWith({ q: 'ali', exclude_project_id: 'p1' })
    expect(search).not.toHaveBeenCalledWith({ q: 'al', exclude_project_id: 'p1' })

    await user.click(screen.getByText('pick'))
    await user.click(screen.getByText('role-custom'))
    await user.click(screen.getByText('submit-add'))
    await waitFor(() =>
      expect(pApi.addMember).toHaveBeenCalledWith('p1', { user_id: 'new-u', role: 'viewer' }),
    )
    expect(screen.queryByRole('alert')).not.toBeInTheDocument()
  })

  it('uses user id in the remove confirm when username is missing', async () => {
    const confirmSpy = vi.spyOn(window, 'confirm').mockReturnValue(true)
    const user = userEvent.setup()
    renderModal()
    await user.click(await screen.findByText('remove-noname'))
    expect(confirmSpy).toHaveBeenCalledWith('Удалить участника u-x из проекта?')
    await waitFor(() => expect(pApi.removeMember).toHaveBeenCalledWith('p1', 'u-x'))
  })

  it('shows inline error on remove failure', async () => {
    vi.spyOn(window, 'confirm').mockReturnValue(true)
    pApi.removeMember.mockRejectedValue(new Error('x'))
    const user = userEvent.setup()
    renderModal()
    await user.click(await screen.findByText('remove-named'))
    expect(await screen.findByRole('alert')).toHaveTextContent('Ошибка удаления участника')
  })

  it('ignores close while a mutation is pending, closes afterwards', async () => {
    let resolve: (v: unknown) => void = () => {}
    pApi.updateMemberRole.mockReturnValue(new Promise((r) => (resolve = r)))
    const user = userEvent.setup()
    const onClose = renderModal()
    await user.click(await screen.findByText('change-role'))
    await waitFor(() => expect(screen.getByRole('button', { name: 'Закрыть' })).toBeDisabled())
    // Escape is ignored by Modal while disabled; call close via the × button too (disabled)
    await user.keyboard('{Escape}')
    expect(onClose).not.toHaveBeenCalled()
    await act(async () => {
      resolve({})
    })
    await waitFor(() => expect(screen.getByRole('button', { name: 'Закрыть' })).toBeEnabled())
    await user.click(screen.getByRole('button', { name: 'Закрыть' }))
    expect(onClose).toHaveBeenCalledTimes(1)
  })

  it('opens and closes the member roles sub-modal', async () => {
    const user = userEvent.setup()
    renderModal()
    await user.click(await screen.findByText('manage-roles'))
    expect(screen.getByText('roles-for:yuser')).toBeInTheDocument()
    await user.click(screen.getByText('close-roles'))
    expect(screen.queryByTestId('roles-modal')).not.toBeInTheDocument()
  })
})
