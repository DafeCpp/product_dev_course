import { describe, it, expect, vi, beforeEach } from 'vitest'
import { formatDateTimeSeconds } from '../utils/format'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import AuditLog from './AuditLog'
import { runEventsApi, captureSessionEventsApi } from '../api/client'

vi.mock('../api/client', () => ({
  runEventsApi: { list: vi.fn() },
  captureSessionEventsApi: { list: vi.fn() },
}))

function evt(id: string, type: string, over: Record<string, unknown> = {}) {
  return { id, event_type: type, created_at: '2026-01-02T10:00:00Z', actor_id: 'u1', actor_role: 'owner', payload: null, ...over }
}

function renderLog(props: Partial<React.ComponentProps<typeof AuditLog>> = {}) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } })
  return render(
    <QueryClientProvider client={qc}>
      <AuditLog runId="r1" defaultCollapsed={false} {...props} />
    </QueryClientProvider>
  )
}

describe('AuditLog', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    vi.mocked(runEventsApi.list).mockResolvedValue({ events: [], total: 0 } as never)
    vi.mocked(captureSessionEventsApi.list).mockResolvedValue({ events: [], total: 0 } as never)
  })

  it('stays collapsed by default without fetching, and expands on click', async () => {
    const user = userEvent.setup()
    vi.mocked(runEventsApi.list).mockResolvedValue({ events: [evt('1', 'run.started')], total: 1 } as never)
    renderLog({ defaultCollapsed: true })
    expect(runEventsApi.list).not.toHaveBeenCalled()
    expect(screen.queryByText('Событий пока нет')).not.toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: /История событий/ }))
    expect(await screen.findByText('Run запущен')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: /История событий/ }))
    expect(screen.queryByText('Run запущен')).not.toBeInTheDocument()
  })

  it('shows loading, then the empty state', async () => {
    renderLog()
    expect(screen.getByText('Загрузка событий...')).toBeInTheDocument()
    expect(await screen.findByText('Событий пока нет')).toBeInTheDocument()
  })

  it('shows an error', async () => {
    vi.mocked(runEventsApi.list).mockRejectedValue(new Error('boom'))
    renderLog()
    expect(await screen.findByText('Не удалось загрузить события')).toBeInTheDocument()
    expect(screen.queryByText('Событий пока нет')).not.toBeInTheDocument()
  })

  it('renders events with labels, actors, formatted time, count and payload', async () => {
    vi.mocked(runEventsApi.list).mockResolvedValue({
      events: [
        evt('1', 'run.started'),
        evt('2', 'run.tags_updated', { payload: { tags: ['a'] } }),
        evt('3', 'run.custom', { payload: {} }),
        evt('4', 'run.status_changed'),
        evt('5', 'run.archived'),
        evt('6', 'run.finished'),
        evt('7', 'step.failed'),
        evt('8', 'capture_session.created'),
        evt('9', 'capture_session.stopped'),
      ],
      total: 9,
    } as never)
    const { container } = renderLog({ title: 'Лог' })
    expect(await screen.findByText('Run запущен')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Лог/ })).toHaveTextContent('(9)')
    expect(screen.getByText('Теги обновлены')).toBeInTheDocument()
    expect(screen.getByText('Статус изменён')).toBeInTheDocument()
    expect(screen.getByText('Run архивирован')).toBeInTheDocument()
    expect(screen.getByText('Run завершён')).toBeInTheDocument()
    expect(screen.getByText('Сессия создана')).toBeInTheDocument()
    expect(screen.getByText('Сессия остановлена')).toBeInTheDocument()
    // unknown types fall back to the raw name (label and badge)
    expect(screen.getAllByText('run.custom').length).toBeGreaterThan(0)
    expect(screen.getAllByText(formatDateTimeSeconds('2026-01-02T10:00:00Z')).length).toBe(9)
    expect(container.querySelectorAll('.audit-log__payload')).toHaveLength(1)
    const dots = Array.from(container.querySelectorAll('.audit-log__dot')).map((d) => d.className.replace('audit-log__dot', '').trim())
    expect(dots).toEqual([
      'audit-log__dot--create', 'audit-log__dot--tag', '', 'audit-log__dot--status', 'audit-log__dot--delete',
      'audit-log__dot--status', 'audit-log__dot--error', 'audit-log__dot--create', 'audit-log__dot--status',
    ])
  })

  it('paginates through run events', async () => {
    const user = userEvent.setup()
    vi.mocked(runEventsApi.list).mockResolvedValue({ events: [evt('1', 'run.started')], total: 45 } as never)
    renderLog()
    expect(await screen.findByText('1 / 3')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: '← Назад' })).toBeDisabled()
    await user.click(screen.getByRole('button', { name: 'Вперёд →' }))
    await waitFor(() => expect(runEventsApi.list).toHaveBeenLastCalledWith('r1', { page: 2, page_size: 20 }))
    expect(await screen.findByText('2 / 3')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'Вперёд →' }))
    expect(await screen.findByText('3 / 3')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Вперёд →' })).toBeDisabled()
    await user.click(screen.getByRole('button', { name: '← Назад' }))
    expect(await screen.findByText('2 / 3')).toBeInTheDocument()
  })

  it('hides pagination for a single page', async () => {
    vi.mocked(runEventsApi.list).mockResolvedValue({ events: [evt('1', 'run.started')], total: 5 } as never)
    renderLog()
    await screen.findByText('Run запущен')
    expect(screen.queryByText(/1 \/ 1/)).not.toBeInTheDocument()
  })

  it('uses the capture session endpoint when a session id is given', async () => {
    vi.mocked(captureSessionEventsApi.list).mockResolvedValue({ events: [evt('s1', 'capture_session.created')], total: 1 } as never)
    renderLog({ captureSessionId: 'cs1' })
    expect(await screen.findByText('Сессия создана')).toBeInTheDocument()
    expect(captureSessionEventsApi.list).toHaveBeenCalledWith('r1', 'cs1', { page: 1, page_size: 20 })
    expect(runEventsApi.list).not.toHaveBeenCalled()
  })

  it('shows a session-mode error', async () => {
    vi.mocked(captureSessionEventsApi.list).mockRejectedValue(new Error('x'))
    renderLog({ captureSessionId: 'cs1' })
    expect(await screen.findByText('Не удалось загрузить события')).toBeInTheDocument()
  })

  it('does not fetch without a run id', () => {
    renderLog({ runId: undefined })
    expect(runEventsApi.list).not.toHaveBeenCalled()
    expect(captureSessionEventsApi.list).not.toHaveBeenCalled()
  })
})
