import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import CaptureSessionTimeline from './CaptureSessionTimeline'
import SensorStatusSummaryBar from './SensorStatusSummaryBar'
import PermissionPicker from './PermissionPicker'
import type { CaptureSession } from '../types'

vi.mock('../api/permissions', () => ({ permissionsApi: { listPermissions: vi.fn() } }))
import { permissionsApi } from '../api/permissions'

const session = (over: Record<string, unknown>) => ({
  id: 's1', ordinal_number: 1, status: 'succeeded',
  started_at: '2026-01-01T10:00:00Z', stopped_at: '2026-01-01T10:10:00Z', ...over,
}) as unknown as CaptureSession

describe('CaptureSessionTimeline', () => {
  beforeEach(() => { vi.useFakeTimers({ toFake: ['Date'] }); vi.setSystemTime(new Date('2026-01-01T12:00:00Z')) })
  afterEach(() => vi.useRealTimers())

  it('renders nothing for no sessions or sessions without a start', () => {
    const { container, rerender } = render(<CaptureSessionTimeline sessions={[]} />)
    expect(container).toBeEmptyDOMElement()
    rerender(<CaptureSessionTimeline sessions={[session({ started_at: null })]} />)
    expect(container).toBeEmptyDOMElement()
  })

  it('draws sorted bars with status titles, durations and a legend', () => {
    render(<CaptureSessionTimeline sessions={[
      session({ id: 'b', ordinal_number: 2, status: 'failed', started_at: '2026-01-01T10:20:00Z', stopped_at: '2026-01-01T11:30:00Z' }),
      session({ id: 'a', ordinal_number: 1, status: 'succeeded', started_at: '2026-01-01T10:00:00Z', stopped_at: '2026-01-01T10:00:30Z' }),
      session({ id: 'c', ordinal_number: 3, status: 'mystery', started_at: '2026-01-01T11:40:00Z', stopped_at: null }),
    ]} />)
    const bars = screen.getAllByRole('button')
    expect(bars).toHaveLength(3)
    expect(bars[0]).toHaveAttribute('title', '#1 · Завершён · 30с')
    expect(bars[1]).toHaveAttribute('title', '#2 · Ошибка · 1ч 10м')
    expect(bars[2]).toHaveAttribute('title', '#3 · mystery · 20м 0с')
    expect(bars[2]).toHaveStyle({ backgroundColor: '#94a3b8' })
    expect(screen.getByText('Выполняется')).toBeInTheDocument()
    expect(screen.getByText('Догрузка')).toBeInTheDocument()
    expect(screen.getByText('#2')).toBeInTheDocument()   // wide bar gets a label
  })

  it('filters by the visible time range and reports clicks and key presses', async () => {
    const onClick = vi.fn()
    render(<CaptureSessionTimeline onSessionClick={onClick}
      timeRange={['2026-01-01T10:05:00Z', '2026-01-01T10:15:00Z']}
      sessions={[
        session({ id: 'in', ordinal_number: 1 }),
        session({ id: 'out', ordinal_number: 2, started_at: '2026-01-01T11:00:00Z', stopped_at: '2026-01-01T11:05:00Z' }),
      ]} />)
    const bars = screen.getAllByRole('button')
    expect(bars).toHaveLength(1)
    await userEvent.click(bars[0])
    bars[0].focus()
    await userEvent.keyboard('{Enter}')
    await userEvent.keyboard(' ')
    await userEvent.keyboard('x')
    expect(onClick).toHaveBeenCalledTimes(3)
    expect(onClick).toHaveBeenCalledWith('in')
  })

  it('does not throw without a click handler and shows a dash for a negative duration', async () => {
    render(<CaptureSessionTimeline sessions={[session({ stopped_at: '2026-01-01T09:00:00Z' })]} />)
    const bar = screen.getByRole('button')
    expect(bar.getAttribute('title')).toContain('—')
    await userEvent.click(bar)
  })
})

describe('SensorStatusSummaryBar', () => {
  it('shows every counter', () => {
    render(<SensorStatusSummaryBar summary={{ online: 3, delayed: 2, offline: 1, total: 6 } as never} />)
    expect(screen.getByText(/Онлайн:/).textContent).toContain('3')
    expect(screen.getByText(/Задержка:/).textContent).toContain('2')
    expect(screen.getByText(/Оффлайн:/).textContent).toContain('1')
    expect(screen.getByText(/Всего:/).textContent).toContain('6')
  })
})

describe('PermissionPicker', () => {
  const perms = [
    { id: '1', name: 'runs.read', scope: 'system', category: 'runs', description: 'читать' },
    { id: '2', name: 'runs.write', scope: 'system', category: 'runs', description: '' },
    { id: '3', name: 'users.read', scope: 'system', category: 'users', description: '' },
    { id: '4', name: 'proj.read', scope: 'project', category: 'proj', description: '' },
  ]
  const wrap = (ui: React.ReactNode) => render(
    <QueryClientProvider client={new QueryClient({ defaultOptions: { queries: { retry: false } } })}>{ui}</QueryClientProvider>)

  beforeEach(() => { vi.clearAllMocks(); vi.mocked(permissionsApi.listPermissions).mockResolvedValue(perms as never) })

  it('groups by category for the scope and toggles single items', async () => {
    const onChange = vi.fn()
    wrap(<PermissionPicker scope="system" selected={['runs.read']} onChange={onChange} />)
    expect(screen.getByText('Загрузка permissions...')).toBeInTheDocument()
    expect(await screen.findByText('users.read')).toBeInTheDocument()
    expect(screen.queryByText('proj.read')).toBeNull()
    expect(screen.getByText('читать')).toBeInTheDocument()
    await userEvent.click(screen.getByLabelText(/runs\.write/))
    expect(onChange).toHaveBeenLastCalledWith(['runs.read', 'runs.write'])
    await userEvent.click(screen.getByLabelText(/runs\.read/))
    expect(onChange).toHaveBeenLastCalledWith([])
  })

  it('toggles a whole group and marks it indeterminate when partly selected', async () => {
    const onChange = vi.fn()
    wrap(<PermissionPicker scope="system" selected={['runs.read']} onChange={onChange} />)
    const group = (await screen.findByText('runs')).closest('label')!.querySelector('input') as HTMLInputElement
    expect(group.indeterminate).toBe(true)
    await userEvent.click(group)
    expect(onChange).toHaveBeenLastCalledWith(['runs.read', 'runs.write'])
    const users = screen.getByText('users').closest('label')!.querySelector('input') as HTMLInputElement
    await userEvent.click(users)
    expect(onChange).toHaveBeenLastCalledWith(['runs.read', 'users.read'])
  })

  it('unselects a fully selected group', async () => {
    const onChange = vi.fn()
    wrap(<PermissionPicker scope="system" selected={['runs.read', 'runs.write', 'users.read']} onChange={onChange} />)
    const group = (await screen.findByText('runs')).closest('label')!.querySelector('input') as HTMLInputElement
    expect(group.checked).toBe(true)
    await userEvent.click(group)
    expect(onChange).toHaveBeenLastCalledWith(['users.read'])
  })

  it('ignores interaction when disabled and shows the empty state', async () => {
    const onChange = vi.fn()
    const { unmount } = wrap(<PermissionPicker scope="system" selected={[]} onChange={onChange} disabled />)
    const box = await screen.findByLabelText(/runs\.read/)
    expect(box).toBeDisabled()
    await userEvent.click(box)
    expect(onChange).not.toHaveBeenCalled()
    unmount()
    vi.mocked(permissionsApi.listPermissions).mockResolvedValue([] as never)
    wrap(<PermissionPicker scope="project" selected={[]} onChange={onChange} />)
    expect(await screen.findByText('Нет доступных permissions')).toBeInTheDocument()
  })
})
