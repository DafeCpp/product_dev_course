import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, within } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import UserProfileModal from './UserProfileModal'
import { projectsApi } from '../api/client'
import { authApi } from '../api/auth'

vi.mock('../api/client', () => ({
  projectsApi: { list: vi.fn(), listMembers: vi.fn() },
}))
vi.mock('../api/auth', () => ({
  authApi: { me: vi.fn() },
}))

const list = projectsApi.list as unknown as ReturnType<typeof vi.fn>
const listMembers = projectsApi.listMembers as unknown as ReturnType<typeof vi.fn>
const me = authApi.me as unknown as ReturnType<typeof vi.fn>

function renderModal() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } })
  return render(
    <QueryClientProvider client={qc}>
      <UserProfileModal isOpen onClose={vi.fn()} />
    </QueryClientProvider>,
  )
}

const project = (id: string, owner: string) => ({
  id,
  name: `Name ${id}`,
  description: null,
  owner_id: owner,
  created_at: '2024-01-01T00:00:00Z',
})

describe('UserProfileModal — branches', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    me.mockResolvedValue({ id: 'u1', username: 'me', email: 'me@x.test' })
  })

  it('uses the fallback message when the error has no message', async () => {
    me.mockRejectedValue('plain string')
    list.mockResolvedValue({ projects: [] })
    renderModal()
    expect(await screen.findByText('Ошибка загрузки данных профиля')).toBeInTheDocument()
  })

  it('shows the projects error message', async () => {
    list.mockRejectedValue(new Error('projects down'))
    renderModal()
    expect(await screen.findByText('projects down')).toBeInTheDocument()
  })

  it('defaults to viewer when user is not a member or has only custom roles', async () => {
    list.mockResolvedValue({ projects: [project('p1', 'other'), project('p2', 'other')] })
    listMembers.mockImplementation((id: string) =>
      Promise.resolve(
        id === 'p1'
          ? { members: [{ user_id: 'someone', roles: ['editor'] }] }
          : { members: [{ user_id: 'u1', roles: ['custom-role'] }] },
      ),
    )
    renderModal()
    const row1 = (await screen.findByText('Name p1')).closest('tr') as HTMLElement
    const row2 = screen.getByText('Name p2').closest('tr') as HTMLElement
    await screen.findAllByText('Наблюдатель')
    expect(within(row1).getByText('Наблюдатель')).toBeInTheDocument()
    expect(within(row2).getByText('Наблюдатель')).toBeInTheDocument()
    expect(within(row1).getByText('Нет описания')).toBeInTheDocument()
    expect(screen.queryByText('Дата регистрации')).not.toBeInTheDocument()
  })
})
