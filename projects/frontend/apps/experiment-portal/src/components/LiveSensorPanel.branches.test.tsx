import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import type { ReactNode } from 'react'
import LiveSensorPanel from './LiveSensorPanel'
import type { Sensor } from '../types/index'
import { sensorsApi } from '../api/client'

vi.mock('../api/client', () => ({
  sensorsApi: {
    getStatusSummary: vi.fn(),
    list: vi.fn(),
    getHeartbeatHistory: vi.fn(),
    getErrorLog: vi.fn(),
  },
}))

const NOW = new Date('2026-01-01T12:00:00Z')
const ago = (s: number) => new Date(NOW.getTime() - s * 1000).toISOString()

const sensor = (over: Record<string, unknown>) =>
  ({
    id: 's1',
    name: 'Temp',
    type: 'temperature',
    display_unit: 'V',
    connection_status: 'online',
    last_heartbeat: ago(2),
    ...over,
  }) as unknown as Sensor

function wrap(ui: ReactNode) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } })
  return render(<QueryClientProvider client={qc}>{ui}</QueryClientProvider>)
}

describe('LiveSensorPanel branches', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    vi.useFakeTimers({ toFake: ['Date'] })
    vi.setSystemTime(NOW)
    vi.mocked(sensorsApi.getStatusSummary).mockResolvedValue(undefined as never)
    vi.mocked(sensorsApi.list).mockResolvedValue({ sensors: [] } as never)
    vi.mocked(sensorsApi.getHeartbeatHistory).mockResolvedValue({
      timestamps: [ago(60), ago(120), ago(7200), ago(-600)],
    } as never)
    vi.mocked(sensorsApi.getErrorLog).mockResolvedValue({} as never)
  })
  afterEach(() => vi.useRealTimers())

  it('falls back for unknown statuses, sensors missing from fresh list and flat values', async () => {
    const sensors = [
      sensor({ id: 'a', name: 'Alpha', connection_status: 'weird' }),
      sensor({ id: 'b', name: 'Beta', connection_status: 'online' }),
      sensor({ id: 'c', name: 'Gamma', connection_status: 'weird' }),
    ]
    vi.mocked(sensorsApi.list).mockResolvedValue({
      sensors: [sensors[0], sensors[2]],
    } as never)
    const { container } = wrap(
      <LiveSensorPanel
        sensors={sensors}
        projectId="p"
        recentValues={{ a: [3, 3, 3], b: [] }}
      />
    )
    await waitFor(() =>
      expect(container.querySelector('.lsp__dot--offline')).toBeInTheDocument()
    )
    expect(container.querySelectorAll('.lsp__dot--weird')).toHaveLength(2)
    const dot = container.querySelector<HTMLElement>('.lsp__dot--weird')
    expect(dot?.style.backgroundColor).toBe('rgb(107, 114, 128)')
    expect(screen.getByText('3.0 – 3.0')).toBeInTheDocument()
    await waitFor(() =>
      expect(container.querySelectorAll('svg.lsp__sparkline').length).toBe(3)
    )
    expect(container.querySelector('.lsp__summary')).not.toBeInTheDocument()
    expect(container.querySelector('.lsp__error-badge')).not.toBeInTheDocument()
  })
})
