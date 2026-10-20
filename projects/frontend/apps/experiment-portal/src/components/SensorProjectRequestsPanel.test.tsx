import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import SensorProjectRequestsPanel from './SensorProjectRequestsPanel'
import { sensorsApi } from '../api/client'
import { notifyError } from '../utils/notify'

vi.mock('../api/client', () => ({
  sensorsApi: { listProjectRequests: vi.fn(), decideProjectRequest: vi.fn(), cancelProjectRequest: vi.fn() },
}))
vi.mock('../utils/notify', () => ({ notifyError: vi.fn(), notifySuccess: vi.fn() }))

function request(over: Record<string, unknown> = {}) {
  return {
    id: 'r1', sensor_id: 's1', project_id: 'p2', requested_by: 'u1', status: 'pending',
    created_at: '2026-10-20T10:00:00Z', sensor_name: 'Thermo', ...over,
  }
}

function mockLists(incoming: unknown[], outgoing: unknown[]) {
  vi.mocked(sensorsApi.listProjectRequests).mockImplementation(async ({ direction }) =>
    ({ requests: direction === 'incoming' ? incoming : outgoing }) as never
  )
}

function renderPanel() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } })
  return render(
    <QueryClientProvider client={qc}>
      <SensorProjectRequestsPanel projectId="p1" projectNames={{ p2: 'Second' }} />
    </QueryClientProvider>
  )
}

describe('SensorProjectRequestsPanel', () => {
  beforeEach(() => vi.clearAllMocks())

  it('renders nothing without pending requests', async () => {
    mockLists([], [])
    const { container } = renderPanel()
    await waitFor(() => expect(sensorsApi.listProjectRequests).toHaveBeenCalledTimes(2))
    expect(container).toBeEmptyDOMElement()
    expect(sensorsApi.listProjectRequests).toHaveBeenCalledWith({
      project_id: 'p1', direction: 'incoming', status: 'pending',
    })
    expect(sensorsApi.listProjectRequests).toHaveBeenCalledWith({
      project_id: 'p1', direction: 'outgoing', status: 'pending',
    })
  })

  it('approves and rejects incoming requests from this project', async () => {
    const user = userEvent.setup()
    mockLists([request(), request({ id: 'r2', sensor_name: null, project_id: 'p9' })], [])
    vi.mocked(sensorsApi.decideProjectRequest).mockResolvedValue(request({ status: 'approved' }) as never)
    renderPanel()

    const item = (await screen.findByText('Thermo')).closest('li') as HTMLElement
    expect(within(item).getByText('Second')).toBeInTheDocument()
    // Unknown project and missing sensor name fall back to ids.
    const second = screen.getByText('p9').closest('li') as HTMLElement
    expect(within(second).getByText('s1')).toBeInTheDocument()

    await user.click(within(item).getByRole('button', { name: 'Разрешить' }))
    await waitFor(() => expect(sensorsApi.decideProjectRequest).toHaveBeenCalledWith('r1', 'approve', 'p1'))
    await user.click(within(second).getByRole('button', { name: 'Отклонить' }))
    await waitFor(() => expect(sensorsApi.decideProjectRequest).toHaveBeenCalledWith('r2', 'reject', 'p1'))
  })

  it('cancels outgoing requests and reports failures', async () => {
    const user = userEvent.setup()
    mockLists([], [request({ id: 'r3', project_id: 'p1', sensor_id: 'foreign-sensor', sensor_name: null })])
    vi.mocked(sensorsApi.cancelProjectRequest).mockRejectedValue({ response: { status: 409 } })
    renderPanel()

    expect(await screen.findByText('Ожидают подтверждения владельца датчика')).toBeInTheDocument()
    expect(screen.getByText('foreign-sensor')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'Отозвать' }))
    await waitFor(() => expect(sensorsApi.cancelProjectRequest).toHaveBeenCalledWith('r3', 'p1'))
    await waitFor(() => expect(notifyError).toHaveBeenCalled())
  })
})
