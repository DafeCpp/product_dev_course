import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { MemoryRouter } from 'react-router-dom'
import Scripts from './Scripts'
import { scriptsApi } from '../api/scripts'
import { pickMaterialSelectOption } from '../testUtils/materialSelect'

vi.mock('../api/scripts', () => ({
  scriptsApi: {
    listScripts: vi.fn(),
    updateScript: vi.fn(),
    deleteScript: vi.fn(),
    listExecutions: vi.fn(),
  },
}))

let granted = new Set<string>()
let permsLoading = false
vi.mock('../hooks/usePermissions', () => ({
  usePermissions: () => ({
    hasSystemPermission: (p: string) => granted.has(p),
    isLoading: permsLoading,
  }),
}))

vi.mock('../components/PermissionGate', () => ({
  default: ({ children }: { children: React.ReactNode }) => <>{children}</>,
}))

vi.mock('./scripts/ScriptFormModal', () => ({
  default: ({
    script,
    onClose,
    onSaved,
  }: {
    script: { name: string } | null
    onClose: () => void
    onSaved: () => void
  }) => (
    <div data-testid="form-modal">
      <span>{script ? `edit:${script.name}` : 'create'}</span>
      <button onClick={onClose}>form-close</button>
      <button onClick={onSaved}>form-saved</button>
    </div>
  ),
}))

vi.mock('./scripts/ScriptExecuteModal', () => ({
  default: ({
    scripts,
    onClose,
    onExecuted,
  }: {
    scripts: { name: string }[]
    onClose: () => void
    onExecuted: () => void
  }) => (
    <div data-testid="exec-modal">
      <span>scripts:{scripts.map((s) => s.name).join(',')}</span>
      <button onClick={onClose}>exec-close</button>
      <button onClick={onExecuted}>exec-done</button>
    </div>
  ),
}))

vi.mock('./scripts/ScriptExecDetailModal', () => ({
  default: ({
    scriptName,
    onClose,
    onCancelled,
  }: {
    scriptName: string
    onClose: () => void
    onCancelled: () => void
  }) => (
    <div data-testid="detail-modal">
      <span>name:{scriptName}</span>
      <button onClick={onClose}>detail-close</button>
      <button onClick={onCancelled}>detail-cancelled</button>
    </div>
  ),
}))

type S = { id: string; name: string; is_active: boolean }
vi.mock('./scripts/ScriptsTable', () => ({
  default: ({
    scripts,
    onEdit,
    onToggleActive,
    onDelete,
  }: {
    scripts: S[]
    onEdit: (s: S) => void
    onToggleActive: (s: S) => void
    onDelete: (s: S) => void
  }) => (
    <div data-testid="scripts-table">
      {scripts.map((s) => (
        <div key={s.id}>
          <span>{s.name}</span>
          <button onClick={() => onEdit(s)}>edit-{s.id}</button>
          <button onClick={() => onToggleActive(s)}>toggle-{s.id}</button>
          <button onClick={() => onDelete(s)}>delete-{s.id}</button>
        </div>
      ))}
    </div>
  ),
}))

type E = { id: string; script_id: string; status: string }
vi.mock('./scripts/ExecutionsTable', () => ({
  default: ({
    executions,
    scriptName,
    onRowClick,
  }: {
    executions: E[]
    scriptName: (id: string) => string
    onRowClick: (e: E) => void
  }) => (
    <div data-testid="exec-table">
      {executions.map((e) => (
        <button key={e.id} onClick={() => onRowClick(e)}>
          row-{e.id}:{scriptName(e.script_id)}
        </button>
      ))}
    </div>
  ),
}))

vi.mock('../utils/notify', () => ({
  notifyError: vi.fn(),
  notifySuccess: vi.fn(),
}))

const api = scriptsApi as unknown as Record<string, ReturnType<typeof vi.fn>>

const SCRIPT_A: S = { id: 'sa', name: 'Alpha', is_active: true }
const SCRIPT_B: S = { id: 'sb', name: 'Beta', is_active: false }

function renderPage() {
  const qc = new QueryClient({
    defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
  })
  return render(
    <QueryClientProvider client={qc}>
      <MemoryRouter>
        <Scripts />
      </MemoryRouter>
    </QueryClientProvider>,
  )
}

describe('Scripts — branches', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    granted = new Set(['scripts.execute', 'scripts.manage'])
    permsLoading = false
    api.listScripts.mockResolvedValue({ scripts: [SCRIPT_A, SCRIPT_B] })
    api.listExecutions.mockResolvedValue({ executions: [] })
    api.updateScript.mockImplementation((id: string, body: { is_active: boolean }) =>
      Promise.resolve({ ...(id === 'sa' ? SCRIPT_A : SCRIPT_B), ...body }),
    )
    api.deleteScript.mockResolvedValue(undefined)
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('shows permissions loading', () => {
    permsLoading = true
    renderPage()
    expect(screen.getByText('Проверка прав доступа...')).toBeInTheDocument()
  })

  it('shows no access without either permission', () => {
    granted = new Set()
    renderPage()
    expect(screen.getByText('Нет доступа')).toBeInTheDocument()
  })

  it('allows access with only scripts.execute', async () => {
    granted = new Set(['scripts.execute'])
    renderPage()
    expect(await screen.findByText('Alpha')).toBeInTheDocument()
  })

  it('allows access with only scripts.manage', async () => {
    granted = new Set(['scripts.manage'])
    renderPage()
    expect(await screen.findByText('Alpha')).toBeInTheDocument()
  })

  it('registry: error and empty states', async () => {
    api.listScripts.mockRejectedValueOnce(new Error('x'))
    const { unmount } = renderPage()
    expect(await screen.findByText('Ошибка загрузки скриптов')).toBeInTheDocument()
    unmount()
    api.listScripts.mockResolvedValueOnce({ scripts: [] })
    renderPage()
    expect(await screen.findByText('Скриптов не найдено')).toBeInTheDocument()
  })

  it('registry: filters by service and active switch', async () => {
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('Alpha')
    await user.type(screen.getByLabelText('Фильтр по сервису'), 'x')
    await waitFor(() =>
      expect(api.listScripts).toHaveBeenLastCalledWith({ target_service: 'x', is_active: undefined }),
    )
    await user.click(screen.getByRole('switch'))
    await waitFor(() =>
      expect(api.listScripts).toHaveBeenLastCalledWith({ target_service: 'x', is_active: true }),
    )
    await user.click(screen.getByRole('switch'))
    expect(screen.getByText('Все')).toBeInTheDocument()
  })

  it('registry: toggle activate/deactivate, delete confirm, edit/create modals', async () => {
    const confirmSpy = vi.spyOn(window, 'confirm').mockReturnValueOnce(false).mockReturnValueOnce(true)
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('Alpha')
    await user.click(screen.getByText('toggle-sa'))
    await waitFor(() => expect(api.updateScript).toHaveBeenCalledWith('sa', { is_active: false }))
    await user.click(screen.getByText('toggle-sb'))
    await waitFor(() => expect(api.updateScript).toHaveBeenCalledWith('sb', { is_active: true }))

    await user.click(screen.getByText('delete-sa'))
    expect(api.deleteScript).not.toHaveBeenCalled()
    await user.click(screen.getByText('delete-sa'))
    await waitFor(() => expect(api.deleteScript).toHaveBeenCalledWith('sa'))
    expect(confirmSpy).toHaveBeenCalledWith('Удалить скрипт «Alpha»?')

    await user.click(screen.getByText('edit-sb'))
    expect(screen.getByText('edit:Beta')).toBeInTheDocument()
    await user.click(screen.getByText('form-close'))
    expect(screen.queryByTestId('form-modal')).not.toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: '+ Создать' }))
    expect(screen.getByText('create')).toBeInTheDocument()
    const n = api.listScripts.mock.calls.length
    await user.click(screen.getByText('form-saved'))
    expect(screen.queryByTestId('form-modal')).not.toBeInTheDocument()
    await waitFor(() => expect(api.listScripts.mock.calls.length).toBeGreaterThan(n))
  })

  it('registry: toggle error is handled', async () => {
    api.updateScript.mockRejectedValue(new Error('no'))
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('Alpha')
    await user.click(screen.getByText('toggle-sa'))
    await waitFor(() => expect(api.updateScript).toHaveBeenCalled())
    expect(screen.getByText('Alpha')).toBeInTheDocument()
  })

  it('executions: error and empty states', async () => {
    api.listExecutions.mockRejectedValueOnce(new Error('x'))
    const user = userEvent.setup()
    renderPage()
    await user.click(screen.getByRole('button', { name: 'Выполнения' }))
    expect(await screen.findByText('Ошибка загрузки выполнений')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'Реестр' }))
    expect(screen.getByRole('button', { name: 'Реестр' }).className).toContain('scripts-tabs__tab--active')
    await user.click(screen.getByRole('button', { name: 'Выполнения' }))
    expect(await screen.findByText('Выполнений не найдено')).toBeInTheDocument()
  })

  it('executions: table, script name map, detail modal and status filter', async () => {
    api.listExecutions.mockResolvedValue({
      executions: [
        { id: 'e1', script_id: 'sa', status: 'completed' },
        { id: 'e2', script_id: 'unknown-id', status: 'failed' },
      ],
    })
    const user = userEvent.setup()
    renderPage()
    await user.click(screen.getByRole('button', { name: 'Выполнения' }))
    expect(await screen.findByText('row-e1:Alpha')).toBeInTheDocument()
    expect(screen.getByText('row-e2:unknown-id')).toBeInTheDocument()

    await user.click(screen.getByText('row-e2:unknown-id'))
    expect(screen.getByText('name:unknown-id')).toBeInTheDocument()
    await user.click(screen.getByText('detail-close'))
    expect(screen.queryByTestId('detail-modal')).not.toBeInTheDocument()

    await user.click(screen.getByText('row-e1:Alpha'))
    expect(screen.getByText('name:Alpha')).toBeInTheDocument()
    const n = api.listExecutions.mock.calls.length
    await user.click(screen.getByText('detail-cancelled'))
    expect(screen.queryByTestId('detail-modal')).not.toBeInTheDocument()
    await waitFor(() => expect(api.listExecutions.mock.calls.length).toBeGreaterThan(n))

    await pickMaterialSelectOption(user, 'Статус', 'Ошибка')
    await waitFor(() => expect(api.listExecutions).toHaveBeenLastCalledWith({ status: 'failed' }))
  })

  it('executions: polls while an execution is running', async () => {
    api.listExecutions.mockResolvedValue({
      executions: [{ id: 'e1', script_id: 'sa', status: 'running' }],
    })
    const user = userEvent.setup()
    renderPage()
    await user.click(screen.getByRole('button', { name: 'Выполнения' }))
    await screen.findByText('row-e1:Alpha')
    const n = api.listExecutions.mock.calls.length
    await waitFor(() => expect(api.listExecutions.mock.calls.length).toBeGreaterThan(n), {
      timeout: 3500,
    })
  })

  it('executions: execute modal with active scripts, close and executed', async () => {
    const user = userEvent.setup()
    renderPage()
    await user.click(screen.getByRole('button', { name: 'Выполнения' }))
    await screen.findByText('Выполнений не найдено')
    await user.click(screen.getByRole('button', { name: '+ Запустить' }))
    expect(screen.getByTestId('exec-modal')).toBeInTheDocument()
    await waitFor(() => expect(api.listScripts).toHaveBeenCalledWith({ is_active: true }))
    expect(await screen.findByText('scripts:Alpha,Beta')).toBeInTheDocument()
    await user.click(screen.getByText('exec-close'))
    expect(screen.queryByTestId('exec-modal')).not.toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: '+ Запустить' }))
    const n = api.listExecutions.mock.calls.length
    await user.click(screen.getByText('exec-done'))
    expect(screen.queryByTestId('exec-modal')).not.toBeInTheDocument()
    await waitFor(() => expect(api.listExecutions.mock.calls.length).toBeGreaterThan(n))
  })

  it('executions: execute modal before active scripts load shows empty list', async () => {
    granted = new Set(['scripts.execute'])
    api.listScripts.mockImplementation((f?: { is_active?: boolean }) =>
      f && f.is_active === true && Object.keys(f).length === 1
        ? new Promise(() => {})
        : Promise.resolve({ scripts: [SCRIPT_A] }),
    )
    const user = userEvent.setup()
    renderPage()
    await user.click(screen.getByRole('button', { name: 'Выполнения' }))
    await screen.findByText('Выполнений не найдено')
    await user.click(screen.getByRole('button', { name: '+ Запустить' }))
    expect(screen.getByText('scripts:')).toBeInTheDocument()
  })
})
