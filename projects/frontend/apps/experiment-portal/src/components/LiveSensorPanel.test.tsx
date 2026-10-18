import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor, within } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import LiveSensorPanel from './LiveSensorPanel'
import type { Sensor } from '../types/index'

vi.mock('../api/client', () => ({
  sensorsApi: {
    getStatusSummary: vi.fn(), list: vi.fn(), getHeartbeatHistory: vi.fn(), getErrorLog: vi.fn(),
  },
}))
import { sensorsApi } from '../api/client'

const NOW = new Date('2026-01-01T12:00:00Z')
const ago = (s: number) => new Date(NOW.getTime() - s * 1000).toISOString()

const sensor = (over: Record<string, unknown>) => ({
  id: 's1', name: 'Temp', type: 'temperature', display_unit: '°C',
  connection_status: 'online', last_heartbeat: ago(2), ...over,
}) as unknown as Sensor

function wrap(ui: React.ReactNode) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } })
  return render(<QueryClientProvider client={qc}>{ui}</QueryClientProvider>)
}

describe('LiveSensorPanel', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    vi.useFakeTimers({ toFake: ['Date'] })
    vi.setSystemTime(NOW)
    vi.mocked(sensorsApi.getStatusSummary).mockResolvedValue({ online: 2, delayed: 1, offline: 1, total: 4 } as never)
    vi.mocked(sensorsApi.list).mockResolvedValue({ sensors: [] } as never)
    vi.mocked(sensorsApi.getHeartbeatHistory).mockResolvedValue({ timestamps: [] } as never)
    vi.mocked(sensorsApi.getErrorLog).mockResolvedValue({ entries: [] } as never)
  })
  afterEach(() => vi.useRealTimers())

  it('renders nothing without sensors', () => {
    const { container } = wrap(<LiveSensorPanel sensors={[]} projectId="p" />)
    expect(container).toBeEmptyDOMElement()
  })

  it('shows the summary and orders sensors online first using fresh statuses', async () => {
    const sensors = [
      sensor({ id: 'a', name: 'Alpha', connection_status: 'online', last_heartbeat: null }),
      sensor({ id: 'b', name: 'Beta', connection_status: 'online' }),
      sensor({ id: 'c', name: 'Gamma', connection_status: 'online' }),
    ]
    vi.mocked(sensorsApi.list).mockResolvedValue({ sensors: [
      sensor({ id: 'a', name: 'Alpha', connection_status: 'offline', last_heartbeat: null }),
      sensor({ id: 'b', name: 'Beta', connection_status: 'online' }),
      sensor({ id: 'c', name: 'Gamma', connection_status: 'delayed', last_heartbeat: ago(20) }),
    ] } as never)
    wrap(<LiveSensorPanel sensors={sensors} projectId="p" />)
    await waitFor(() => expect(screen.getByText('Offline')).toBeInTheDocument())
    const names = Array.from(document.querySelectorAll('.lsp__name')).map((n) => n.textContent)
    expect(names).toEqual(['Beta', 'Gamma', 'Alpha'])
    expect(await screen.findByText('2')).toBeInTheDocument()
    expect(screen.getByText('Нет данных')).toBeInTheDocument()
  })

  it('formats heartbeat age and latency levels', async () => {
    const sensors = [
      sensor({ id: 'a', last_heartbeat: ago(2) }),
      sensor({ id: 'b', last_heartbeat: ago(45) }),
      sensor({ id: 'c', last_heartbeat: ago(600) }),
      sensor({ id: 'd', last_heartbeat: ago(7300) }),
      sensor({ id: 'e', last_heartbeat: ago(-30) }),
    ]
    wrap(<LiveSensorPanel sensors={sensors} projectId="p" />)
    expect(screen.getAllByText('Только что')).toHaveLength(2)
    expect(screen.getByText('45с назад')).toBeInTheDocument()
    expect(screen.getByText('10м назад')).toBeInTheDocument()
    expect(screen.getByText('2ч назад')).toBeInTheDocument()
    const latencies = Array.from(document.querySelectorAll('.lsp__latency')).map((n) => [n.textContent, n.className.split('--')[1]])
    expect(latencies).toEqual([['2.0с', 'green'], ['45.0с', 'gray'], ['—', 'gray'], ['—', 'gray'], ['0 ms', 'green']])
  })

  it('shows sub-100 ms latency and amber for slow heartbeats', () => {
    vi.setSystemTime(NOW)
    wrap(<LiveSensorPanel projectId="p" sensors={[
      sensor({ id: 'a', last_heartbeat: new Date(NOW.getTime() - 50).toISOString() }),
      sensor({ id: 'b', last_heartbeat: ago(10) }),
    ]} />)
    const lat = Array.from(document.querySelectorAll('.lsp__latency'))
    expect(lat[0].textContent).toBe('50 ms')
    expect(lat[1].className).toContain('amber')
  })

  it('draws value sparklines and the last value only with enough samples', () => {
    wrap(<LiveSensorPanel projectId="p" sensors={[sensor({ id: 'a' }), sensor({ id: 'b', name: 'B' })]}
      recentValues={{ a: [1, 2, 3.5], b: [7] }} />)
    expect(screen.getAllByText('3.50 °C')).toHaveLength(2)    // sparkline label + last value
    expect(screen.getByText('1.0 – 3.5')).toBeInTheDocument()
    expect(screen.getByText('7.00 °C')).toBeInTheDocument()
    expect(document.querySelectorAll('.lsp__value-sparkline')).toHaveLength(1)
  })

  it('draws the heartbeat sparkline when history is available and the error badge', async () => {
    vi.mocked(sensorsApi.getHeartbeatHistory).mockResolvedValue({ timestamps: [ago(10), ago(70), ago(3000)] } as never)
    vi.mocked(sensorsApi.getErrorLog).mockResolvedValue({ entries: new Array(150).fill({}) } as never)
    wrap(<LiveSensorPanel projectId="p" sensors={[sensor({ id: 'a' })]} />)
    await waitFor(() => expect(document.querySelector('svg.lsp__sparkline')).not.toBeNull())
    expect(await screen.findByText('99+')).toHaveAttribute('title', '150 ошибок за 24ч')
    expect(document.querySelector('.lsp__sparkline--empty')).toBeNull()
  })

  it('shows an exact error count below 100 and no badge for none', async () => {
    vi.mocked(sensorsApi.getErrorLog).mockResolvedValue({ entries: [{}, {}, {}] } as never)
    const { unmount } = wrap(<LiveSensorPanel projectId="p" sensors={[sensor({ id: 'a' })]} />)
    expect(await screen.findByText('3')).toHaveAttribute('title', '3 ошибок за 24ч')
    unmount()
    vi.mocked(sensorsApi.getErrorLog).mockResolvedValue({ entries: [] } as never)
    wrap(<LiveSensorPanel projectId="p" sensors={[sensor({ id: 'a' })]} />)
    await waitFor(() => expect(sensorsApi.getErrorLog).toHaveBeenCalledTimes(2))
    expect(document.querySelector('.lsp__error-badge')).toBeNull()
    expect(within(document.body).queryByText('Нет данных')).toBeNull()
  })

  it('does not query status without a project id', () => {
    wrap(<LiveSensorPanel projectId="" sensors={[sensor({ id: 'a' })]} />)
    expect(sensorsApi.getStatusSummary).not.toHaveBeenCalled()
    expect(sensorsApi.list).not.toHaveBeenCalled()
  })
})
