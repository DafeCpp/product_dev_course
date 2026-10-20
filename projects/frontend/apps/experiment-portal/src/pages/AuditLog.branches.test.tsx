import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter, useLocation } from 'react-router-dom'
import { render, screen, waitFor, within, fireEvent } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { format } from 'date-fns'
import AuditLog from './AuditLog'
import { auditApi } from '../api/audit'
import { pickMaterialSelectOption } from '../testUtils/materialSelect'

vi.mock('../api/audit', () => ({
  auditApi: { queryAuditLog: vi.fn() },
}))

vi.mock('../hooks/usePermissions', () => ({
  usePermissions: () => ({
    hasSystemPermission: () => true,
    isLoading: false,
  }),
}))

const query = auditApi.queryAuditLog as unknown as ReturnType<typeof vi.fn>

function LocationProbe() {
  const loc = useLocation()
  return <div data-testid="loc">{loc.search}</div>
}

function renderPage(url = '/audit') {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } })
  return render(
    <QueryClientProvider client={qc}>
      <MemoryRouter initialEntries={[url]}>
        <AuditLog />
        <LocationProbe />
      </MemoryRouter>
    </QueryClientProvider>,
  )
}

const FULL = {
  id: 'e1',
  actor_id: 'actor-full',
  action: 'project.update',
  scope_type: 'project',
  scope_id: 'p-1',
  target_type: 'experiment',
  target_id: 'x-1',
  ip_address: null,
  user_agent: null,
  details: { a: 1 },
  timestamp: '2026-05-08T12:00:00Z',
}
const PARTIAL = {
  ...FULL,
  id: 'e2',
  actor_id: 'actor-partial',
  action: 'role.grant',
  scope_type: 'system',
  scope_id: null,
  target_type: 'user',
  target_id: null,
  ip_address: '10.0.0.1',
}
const BARE = {
  ...FULL,
  id: 'e3',
  actor_id: 'actor-bare',
  action: 'noop',
  scope_type: null,
  scope_id: null,
  target_type: null,
  target_id: null,
}

describe('AuditLog — branches', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    query.mockResolvedValue({ entries: [FULL, PARTIAL, BARE], total: 120 })
  })

  it('prefills drafts from the URL and passes filters to the API', async () => {
    renderPage('/audit?actor_id=u1&action=user.login&scope_type=system&from=2026-01-01&to=2026-02-01')
    expect(screen.getByText('Загрузка аудит-лога...')).toBeInTheDocument()
    await screen.findByText('project.update')
    expect(screen.getByLabelText('Фильтр по действию')).toHaveValue('user.login')
    expect(screen.getByLabelText('Фильтр по пользователю')).toHaveValue('u1')
    expect(screen.getByLabelText('Дата с')).toHaveValue('2026-01-01')
    expect(screen.getByLabelText('Дата по')).toHaveValue('2026-02-01')
    expect(query).toHaveBeenCalledWith({
      actor_id: 'u1',
      action: 'user.login',
      scope_type: 'system',
      from: '2026-01-01',
      to: '2026-02-01',
      limit: 50,
      offset: 0,
    })
  })

  it('shows an error state', async () => {
    query.mockRejectedValue(new Error('x'))
    renderPage()
    expect(await screen.findByText('Ошибка загрузки аудит-лога')).toBeInTheDocument()
  })

  it('renders scope/target/ip variants in the table', async () => {
    renderPage()
    await screen.findByText('project.update')
    const rows = screen.getAllByRole('button').filter((el) => el.tagName === 'TR')
    expect(rows).toHaveLength(3)
    expect(within(rows[0]).getByText('project / p-1')).toBeInTheDocument()
    expect(within(rows[0]).getByText('experiment / x-1')).toBeInTheDocument()
    expect(within(rows[0]).getByText('—')).toBeInTheDocument()
    expect(within(rows[1]).getByText('system')).toBeInTheDocument()
    expect(within(rows[1]).getByText('user')).toBeInTheDocument()
    expect(within(rows[1]).getByText('10.0.0.1')).toBeInTheDocument()
    expect(within(rows[2]).getAllByText('—')).toHaveLength(3)
    expect(
      within(rows[0]).getByText(format(new Date(FULL.timestamp), 'dd MMM yyyy HH:mm:ss')),
    ).toBeInTheDocument()
  })

  it('applies all filters via the Apply button and Enter keys', async () => {
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('project.update')
    await user.type(screen.getByLabelText('Фильтр по действию'), 'user.login')
    await user.type(screen.getByLabelText('Фильтр по пользователю'), 'u9')
    await pickMaterialSelectOption(user, 'Область', 'Проект')
    fireEvent.change(screen.getByLabelText('Дата с'), { target: { value: '2026-03-01' } })
    fireEvent.change(screen.getByLabelText('Дата по'), { target: { value: '2026-03-31' } })
    await user.click(screen.getByRole('button', { name: 'Применить' }))
    const search = new URLSearchParams(screen.getByTestId('loc').textContent ?? '')
    expect(Object.fromEntries(search)).toEqual({
      actor_id: 'u9',
      action: 'user.login',
      scope_type: 'project',
      from: '2026-03-01',
      to: '2026-03-31',
    })

    // Enter on the actor field re-applies; other keys do nothing.
    await user.clear(screen.getByLabelText('Фильтр по действию'))
    await user.type(screen.getByLabelText('Фильтр по действию'), 'a{Enter}')
    await waitFor(() =>
      expect(new URLSearchParams(screen.getByTestId('loc').textContent ?? '').get('action')).toBe('a'),
    )
    await user.clear(screen.getByLabelText('Фильтр по пользователю'))
    await user.type(screen.getByLabelText('Фильтр по пользователю'), 'u2{Enter}')
    await waitFor(() =>
      expect(new URLSearchParams(screen.getByTestId('loc').textContent ?? '').get('actor_id')).toBe('u2'),
    )

    await user.click(screen.getByRole('button', { name: 'Сбросить' }))
    expect(screen.getByTestId('loc').textContent).toBe('')
    expect(screen.getByLabelText('Фильтр по действию')).toHaveValue('')

    await user.type(screen.getByLabelText('Фильтр по пользователю'), 'solo{Enter}')
    await waitFor(() => expect(screen.getByTestId('loc').textContent).toBe('?actor_id=solo'))
  })

  it('paginates and computes offsets', async () => {
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('project.update')
    expect(screen.getByText('Страница 1 из 3')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'Вперед' }))
    await waitFor(() => expect(query).toHaveBeenLastCalledWith(expect.objectContaining({ offset: 50 })))
  })

  it('opens detail modal via keyboard (Enter, Space) and ignores other keys', async () => {
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('project.update')
    const rows = screen.getAllByRole('button').filter((el) => el.tagName === 'TR')

    fireEvent.keyDown(rows[0], { key: 'a' })
    expect(screen.queryByText('Детали записи аудита')).not.toBeInTheDocument()

    fireEvent.keyDown(rows[0], { key: 'Enter' })
    const modal = document.querySelector('.audit-log-detail') as HTMLElement
    expect(within(modal).getByText('project / p-1')).toBeInTheDocument()
    expect(within(modal).getByText('experiment / x-1')).toBeInTheDocument()
    expect(within(modal).getByText('—')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '×' }))
    expect(screen.queryByText('Детали записи аудита')).not.toBeInTheDocument()

    fireEvent.keyDown(rows[1], { key: ' ' })
    let m = document.querySelector('.audit-log-detail') as HTMLElement
    expect(within(m).getByText('system')).toBeInTheDocument()
    expect(within(m).getByText('user')).toBeInTheDocument()
    expect(within(m).getByText('10.0.0.1')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '×' }))

    await user.click(rows[2])
    m = document.querySelector('.audit-log-detail') as HTMLElement
    expect(within(m).getAllByText('—')).toHaveLength(3)
  })
})
