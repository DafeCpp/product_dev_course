import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen } from '@testing-library/react'
import Plotly from 'plotly.js-dist-min'
import TelemetryHistoryChart from './TelemetryHistoryChart'

vi.mock('plotly.js-dist-min', () => ({
  default: { react: vi.fn(), purge: vi.fn() },
}))

type Props = React.ComponentProps<typeof TelemetryHistoryChart>

const raw = [
  { name: 'A', x: ['t1', 't2'], y: [1, null] },
  { name: 'B', x: ['t1'], y: [2] },
]
const agg = [{ name: 'S / x', x: ['t1', 't2'], avg: [2, 3], min: [1, 2], max: [3, 4] }]

function renderChart(over: Partial<Props> = {}) {
  const props: Props = {
    rawSeries: raw, aggregatedSeries: agg, aggregated: false, loading: false, hasData: true,
    loadedCount: 0, maxPoints: 5000, onTimeRangeChange: vi.fn(), ...over,
  }
  return { props, ...render(<TelemetryHistoryChart {...props} />) }
}

const reactCall = (i = 0) => vi.mocked(Plotly.react).mock.calls[i] as unknown as [HTMLElement, Array<Record<string, any>>, unknown, unknown]

describe('TelemetryHistoryChart', () => {
  beforeEach(() => vi.clearAllMocks())

  it('plots one scattergl trace per raw series with cycling colours', () => {
    renderChart({ rawSeries: Array.from({ length: 7 }, (_, i) => ({ name: `s${i}`, x: ['t'], y: [i] })) })
    const traces = reactCall()[1]
    expect(traces).toHaveLength(7)
    expect(traces[0]).toMatchObject({ type: 'scattergl', name: 's0', line: { color: '#2563eb', width: 2 } })
    expect(traces[6].line.color).toBe('#2563eb')
    expect(traces[1].line.color).toBe('#16a34a')
  })

  it('plots min/max band and avg line for aggregated series', () => {
    renderChart({ aggregated: true })
    const traces = reactCall()[1]
    expect(traces).toHaveLength(3)
    expect(traces[0]).toMatchObject({ y: [1, 2], showlegend: false, hoverinfo: 'skip' })
    expect(traces[1]).toMatchObject({ y: [3, 4], fill: 'tonexty', fillcolor: '#2563eb26', showlegend: false })
    expect(traces[2]).toMatchObject({ y: [2, 3], name: 'S / x', line: { color: '#2563eb', width: 2 } })
  })

  it('plots nothing when there is no data', () => {
    renderChart({ hasData: false })
    expect(reactCall()[1]).toEqual([])
  })

  it('shows the empty hint only when idle without data', () => {
    const { rerender, props } = renderChart({ hasData: false })
    expect(screen.getByText(/Нет данных/)).toBeInTheDocument()
    rerender(<TelemetryHistoryChart {...props} hasData={false} loading />)
    expect(screen.queryByText(/Нет данных/)).not.toBeInTheDocument()
    expect(screen.getByText(/Загрузка истории/)).toBeInTheDocument()
  })

  it('shows loading progress only when points were loaded', () => {
    const { rerender, props } = renderChart({ loading: true })
    expect(screen.queryByText(/\/ 5000/)).not.toBeInTheDocument()
    rerender(<TelemetryHistoryChart {...props} loading loadedCount={120} />)
    expect(screen.getByText(/120 \/ 5000/)).toBeInTheDocument()
  })

  it('hides both overlays when data is shown and idle', () => {
    renderChart()
    expect(screen.queryByText(/Загрузка истории/)).not.toBeInTheDocument()
    expect(screen.queryByText(/Нет данных/)).not.toBeInTheDocument()
  })

  it('reports relayout ranges, resets to null on autorange, and cleans up', () => {
    const handlers: Record<string, (e: Record<string, unknown>) => void> = {}
    const removeAll = vi.fn()
    vi.mocked(Plotly.react).mockImplementationOnce(((el: HTMLElement) => {
      Object.assign(el, {
        on: (ev: string, h: (e: Record<string, unknown>) => void) => { handlers[ev] = h },
        removeAllListeners: removeAll,
      })
    }) as never)
    const { props, unmount } = renderChart()
    handlers.plotly_relayout({ 'xaxis.range': ['a', 'b'] })
    expect(props.onTimeRangeChange).toHaveBeenLastCalledWith(['a', 'b'])
    handlers.plotly_relayout({ 'xaxis.autorange': true })
    expect(props.onTimeRangeChange).toHaveBeenLastCalledWith(null)
    unmount()
    expect(removeAll).toHaveBeenCalledWith('plotly_relayout')
    expect(Plotly.purge).toHaveBeenCalled()
  })

  it('still cleans up when the plot element has no event API', () => {
    const { unmount } = renderChart()
    unmount()
    expect(Plotly.purge).toHaveBeenCalled()
  })
})
