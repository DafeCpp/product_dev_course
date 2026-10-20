import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ProjectsList from './ProjectsList'
import { projectsApi } from '../api/client'
import { authApi } from '../api/auth'
import { pickMaterialSelectOption } from '../testUtils/materialSelect'

vi.mock('../api/client', () => ({
  projectsApi: { list: vi.fn() },
}))
vi.mock('../api/auth', () => ({
  authApi: { me: vi.fn() },
}))

vi.mock('../components/ProjectModal', () => ({
  default: ({
    isOpen,
    mode,
    projectId,
    onClose,
  }: {
    isOpen: boolean
    mode: string
    projectId?: string
    onClose: () => void
  }) =>
    isOpen ? (
      <div data-testid="project-modal">
        <span>
          mode:{mode}:{projectId ?? 'none'}
        </span>
        <button onClick={onClose}>close-project-modal</button>
      </div>
    ) : null,
}))

vi.mock('../components/ProjectMembersModal', () => ({
  default: ({
    projectId,
    projectOwnerId,
    onClose,
  }: {
    projectId: string
    projectOwnerId: string
    onClose: () => void
  }) => (
    <div data-testid="members-modal">
      <span>
        members:{projectId}:{projectOwnerId}
      </span>
      <button onClick={onClose}>close-members</button>
    </div>
  ),
}))

const list = projectsApi.list as unknown as ReturnType<typeof vi.fn>
const me = authApi.me as unknown as ReturnType<typeof vi.fn>

const owned = {
  id: 'p-own',
  name: 'Owned',
  description: null,
  owner_id: 'u1',
  created_at: '2024-01-01T00:00:00Z',
}
const shared = {
  id: 'p-shared',
  name: 'Shared',
  description: 'Has text',
  owner_id: 'u2',
  created_at: '2024-01-01T00:00:00Z',
}

function renderPage() {
  const qc = new QueryClient({
    defaultOptions: { queries: { retry: false } },
  })
  return render(
    <QueryClientProvider client={qc}>
      <ProjectsList />
    </QueryClientProvider>,
  )
}

describe('ProjectsList — branches', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    list.mockResolvedValue({ projects: [owned, shared] })
    me.mockResolvedValue({ id: 'u1' })
  })

  it('shows error state after the retry fails', async () => {
    list.mockRejectedValue(new Error('x'))
    renderPage()
    expect(screen.getByText('Загрузка проектов...')).toBeInTheDocument()
    expect(
      await screen.findByText('Ошибка загрузки проектов', {}, { timeout: 4000 }),
    ).toBeInTheDocument()
  })

  it('empty state button opens create modal', async () => {
    list.mockResolvedValue({ projects: [] })
    const user = userEvent.setup()
    renderPage()
    await user.click(await screen.findByRole('button', { name: 'Создать первый проект' }))
    expect(screen.getByText('mode:create:none')).toBeInTheDocument()
    await user.click(screen.getByText('close-project-modal'))
    expect(screen.queryByTestId('project-modal')).not.toBeInTheDocument()
  })

  it('renders owner vs participant cards, description fallback and modals', async () => {
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('Owned')
    expect(screen.getByText('Владелец')).toBeInTheDocument()
    expect(screen.getByText('Участник')).toBeInTheDocument()
    expect(screen.getByText(/Добавьте описание проекта/)).toBeInTheDocument()
    expect(screen.getByText('Has text')).toBeInTheDocument()
    await waitFor(() =>
      expect(screen.getAllByRole('button', { name: 'Открыть проект' })[0]).toBeEnabled(),
    )
    const open = screen.getAllByRole('button', { name: 'Открыть проект' })
    expect(open[0]).toHaveAttribute('title', 'Просмотр и редактирование')
    expect(open[1]).toHaveAttribute('title', 'Просмотр')

    await user.click(open[0])
    expect(screen.getByText('mode:edit:p-own')).toBeInTheDocument()
    await user.click(screen.getByText('close-project-modal'))
    await user.click(open[1])
    expect(screen.getByText('mode:view:p-shared')).toBeInTheDocument()
    await user.click(screen.getByText('close-project-modal'))

    expect(screen.getAllByRole('button', { name: 'Управление участниками' })).toHaveLength(1)
    await user.click(screen.getByRole('button', { name: 'Управление участниками' }))
    expect(screen.getByText('members:p-own:u1')).toBeInTheDocument()
    await user.click(screen.getByText('close-members'))
    expect(screen.queryByTestId('members-modal')).not.toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: 'Создать проект' }))
    expect(screen.getByText('mode:create:none')).toBeInTheDocument()
  })

  it('treats all projects as foreign when current user is unknown', async () => {
    me.mockRejectedValue(new Error('no'))
    renderPage()
    await screen.findByText('Owned')
    await waitFor(() => expect(screen.getAllByText('Участник')).toHaveLength(2))
  })

  it('debounces search and applies role filter', async () => {
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('Owned')
    await user.type(screen.getByLabelText('Поиск проектов'), 'ab')
    await waitFor(() =>
      expect(list).toHaveBeenLastCalledWith({ search: 'ab', limit: 20, offset: 0 }),
    )
    expect(list).not.toHaveBeenCalledWith({ search: 'a', limit: 20, offset: 0 })
    await pickMaterialSelectOption(user, 'Роль', 'Editor')
    await waitFor(() =>
      expect(list).toHaveBeenLastCalledWith({ search: 'ab', role: 'editor', limit: 20, offset: 0 }),
    )
  })
})
