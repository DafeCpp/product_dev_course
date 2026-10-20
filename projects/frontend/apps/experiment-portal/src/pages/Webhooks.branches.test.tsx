import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor, act } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { MemoryRouter } from 'react-router-dom'
import Webhooks from './Webhooks'
import { projectsApi, webhooksApi } from '../api/client'

vi.mock('../api/client', () => ({
  webhooksApi: {
    list: vi.fn(),
    create: vi.fn(),
    delete: vi.fn(),
    listDeliveries: vi.fn(),
    retryDelivery: vi.fn(),
  },
  projectsApi: { list: vi.fn() },
}))

vi.mock('../utils/notify', () => ({
  notifyError: vi.fn(),
  notifySuccess: vi.fn(),
}))

const STORAGE_KEY = 'experiment_portal.active_project_id'
const p1 = { id: 'proj-1', name: 'Project 1' }
const p2 = { id: 'proj-2', name: 'Project 2' }

const baseDelivery = {
  subscription_id: 'wh-1',
  project_id: 'proj-1',
  event_type: 'run.started',
  target_url: 'https://example.com/hook',
  attempt_count: 2,
  last_error: null as string | null,
  created_at: '2024-01-15T10:30:00Z',
}

const api = webhooksApi as unknown as Record<string, ReturnType<typeof vi.fn>>
const projList = projectsApi.list as unknown as ReturnType<typeof vi.fn>

function renderPage() {
  const qc = new QueryClient({
    defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
  })
  return render(
    <QueryClientProvider client={qc}>
      <MemoryRouter>
        <Webhooks />
      </MemoryRouter>
    </QueryClientProvider>,
  )
}

describe('Webhooks — branches', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    localStorage.clear()
    projList.mockResolvedValue({ projects: [p1, p2] })
    api.list.mockResolvedValue({ webhooks: [] })
    api.listDeliveries.mockResolvedValue({ deliveries: [], total: 0 })
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('clears a stored project id when there are no projects', async () => {
    localStorage.setItem(STORAGE_KEY, 'stale')
    projList.mockResolvedValue({ projects: [] })
    renderPage()
    expect(screen.getByText('Загрузка проектов...')).toBeInTheDocument()
    expect(
      await screen.findByText(/У вас нет проектов/),
    ).toBeInTheDocument()
    await waitFor(() => expect(localStorage.getItem(STORAGE_KEY)).toBeNull())
    expect(screen.getByText('Выберите проект, чтобы посмотреть доставки.')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Создать' })).toBeDisabled()
  })

  it('falls back to the first project when the stored one is unknown, then switches project', async () => {
    localStorage.setItem(STORAGE_KEY, 'missing')
    const user = userEvent.setup()
    renderPage()
    await waitFor(() =>
      expect(api.list).toHaveBeenCalledWith({ project_id: 'proj-1', page_size: 100 }),
    )
    expect(localStorage.getItem(STORAGE_KEY)).toBe('proj-1')
    await user.selectOptions(screen.getByLabelText('Проект:'), 'proj-2')
    await waitFor(() =>
      expect(api.list).toHaveBeenCalledWith({ project_id: 'proj-2', page_size: 100 }),
    )
    expect(localStorage.getItem(STORAGE_KEY)).toBe('proj-2')
  })

  it('keeps a valid stored project', async () => {
    localStorage.setItem(STORAGE_KEY, 'proj-2')
    renderPage()
    await waitFor(() =>
      expect(api.list).toHaveBeenCalledWith({ project_id: 'proj-2', page_size: 100 }),
    )
    expect(api.list).not.toHaveBeenCalledWith({ project_id: 'proj-1', page_size: 100 })
  })

  it('shows Error-instance messages for subscription and delivery failures', async () => {
    api.list.mockRejectedValue(new Error('subs boom'))
    api.listDeliveries.mockRejectedValue(new Error('del boom'))
    renderPage()
    expect(await screen.findByText('subs boom')).toBeInTheDocument()
    expect(await screen.findByText('del boom')).toBeInTheDocument()
  })

  it('shows fallback messages for non-Error failures', async () => {
    api.list.mockRejectedValue('nope')
    api.listDeliveries.mockRejectedValue({ code: 1 })
    renderPage()
    expect(await screen.findByText('Ошибка загрузки подписок')).toBeInTheDocument()
    expect(await screen.findByText('Ошибка загрузки доставок')).toBeInTheDocument()
  })

  it('renders inactive subscription with secret, and cancels delete', async () => {
    api.list.mockResolvedValue({
      webhooks: [
        {
          id: 'wh-9',
          target_url: 'https://x.test/h',
          has_secret: true,
          event_types: ['run.archived'],
          is_active: false,
        },
      ],
    })
    vi.spyOn(window, 'confirm').mockReturnValue(false)
    const user = userEvent.setup()
    renderPage()
    expect(await screen.findByText('Неактивен')).toBeInTheDocument()
    expect(screen.getByText('secret: ****')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'Удалить' }))
    expect(api.delete).not.toHaveBeenCalled()
  })

  it('create form toggling, validation errors and pending state', async () => {
    let resolveCreate: (v: unknown) => void = () => {}
    api.create.mockReturnValue(new Promise((r) => (resolveCreate = r)))
    const user = userEvent.setup()
    renderPage()
    await waitFor(() => expect(api.list).toHaveBeenCalled())
    await screen.findByText('Доставок пока нет')
    await user.click(screen.getByRole('button', { name: 'Создать первый webhook' }))
    // Empty-state button hidden while form is open; header button shows "Отмена"
    expect(screen.queryByRole('button', { name: 'Создать первый webhook' })).not.toBeInTheDocument()
    const cancelButtons = screen.getAllByRole('button', { name: 'Отмена' })
    expect(cancelButtons).toHaveLength(2)
    // form-level cancel
    await user.click(cancelButtons[1])
    expect(screen.queryByLabelText('Target URL')).not.toBeInTheDocument()

    // header toggle opens again
    await user.click(screen.getByRole('button', { name: 'Создать' }))
    // submit empty → both errors
    await user.click(screen.getByRole('button', { name: 'Создать webhook' }))
    expect(await screen.findByText('URL обязателен')).toBeInTheDocument()
    expect(screen.getByText('Укажите хотя бы один тип события')).toBeInTheDocument()

    await user.type(screen.getByLabelText('Target URL'), 'https://ok.test/h')
    await user.type(screen.getByLabelText('Типы событий (через запятую)'), 'run.started')
    await user.type(screen.getByLabelText('Secret (опционально)'), 'k')
    await user.click(screen.getByRole('button', { name: 'Создать webhook' }))
    expect(await screen.findByRole('button', { name: 'Создание...' })).toBeDisabled()
    expect(api.create).toHaveBeenCalledWith(
      { target_url: 'https://ok.test/h', event_types: ['run.started'], secret: 'k' },
      { project_id: 'proj-1' },
    )
    await act(async () => {
      resolveCreate({ id: 'new' })
    })
    await waitFor(() => expect(screen.queryByLabelText('Target URL')).not.toBeInTheDocument())

    // header toggle: open then close via header "Отмена"
    await user.click(screen.getByRole('button', { name: 'Создать' }))
    expect(screen.getByLabelText('Target URL')).toHaveValue('')
    await user.click(screen.getAllByRole('button', { name: 'Отмена' })[0])
    expect(screen.queryByLabelText('Target URL')).not.toBeInTheDocument()
  })

  it('renders delivery status classes, errors and paginates', async () => {
    api.listDeliveries.mockResolvedValue({
      total: 45,
      deliveries: [
        { ...baseDelivery, id: 'd1', status: 'failed', last_error: 'timeout' },
        { ...baseDelivery, id: 'd2', status: 'dead_lettered' },
        { ...baseDelivery, id: 'd3', status: 'pending' },
        { ...baseDelivery, id: 'd4', status: 'in_progress' },
      ],
    })
    const user = userEvent.setup()
    renderPage()
    expect(await screen.findByText('timeout')).toBeInTheDocument()
    expect(screen.getByText('failed').className).toContain('delivery-status--failed')
    expect(screen.getByText('dead_lettered').className).toContain('delivery-status--failed')
    expect(screen.getByText('pending').className).toContain('delivery-status--pending')
    expect(screen.getByText('in_progress').className).toContain('delivery-status--retrying')
    expect(screen.getAllByRole('button', { name: 'Retry' })).toHaveLength(1)
    expect(screen.getByText('Доставки (45)')).toBeInTheDocument()

    expect(screen.getByText('1 / 3')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: '← Назад' })).toBeDisabled()
    await user.click(screen.getByRole('button', { name: 'Вперёд →' }))
    expect(await screen.findByText('2 / 3')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'Вперёд →' }))
    expect(await screen.findByText('3 / 3')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Вперёд →' })).toBeDisabled()
    await user.click(screen.getByRole('button', { name: '← Назад' }))
    expect(await screen.findByText('2 / 3')).toBeInTheDocument()
    await waitFor(() =>
      expect(api.listDeliveries).toHaveBeenCalledWith(
        expect.objectContaining({ page: 3, page_size: 20 }),
      ),
    )
  })
})
