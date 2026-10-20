import { describe, it, expect, vi, beforeEach } from 'vitest'
import { act, render, screen } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import type { ReactNode } from 'react'
import Plotly from 'plotly.js-dist-min'
import RunMetrics from './RunMetrics'
import { metricsApi } from '../api/client'

vi.mock('../api/client', () => ({
  metricsApi: {
    summary: vi.fn(),
    list: vi.fn(),
    aggregations: vi.fn(),
  },
}))
vi.mock('plotly.js-dist-min', () => ({
  default: { react: vi.fn(), purge: vi.fn() },
}))

const wrapper = ({ children }: { children: ReactNode }) => {
  const queryClient = new QueryClient({ defaultOptions: { queries: { retry: false } } })
  return <QueryClientProvider client={queryClient}>{children}</QueryClientProvider>
}

const RUN_ID = 'run-x'

function item(name: string, total: number, extra: Record<string, unknown> = {}) {
  return {
    name,
    last_step: 1,
    last_value: 1,
    last_timestamp: '2024-01-01T00:00:00Z',
    total_steps: total,
    min_value: 1,
    avg_value: 1,
    max_value: 1,
    ...extra,
  }
}

type PlotlyTrace = { mode: string; x: number[] }

describe('RunMetrics branches', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('renders placeholders for null values and empty points', async () => {
    vi.mocked(metricsApi.summary).mockResolvedValue({
      run_id: RUN_ID,
      metrics: [
        item('m1', 1, {
          last_value: null,
          min_value: null,
          avg_value: null,
          max_value: null,
          last_step: null,
        }),
      ],
    } as never)
    vi.mocked(metricsApi.list).mockResolvedValue({
      run_id: RUN_ID,
      series: [],
      total: 0,
      limit: 1,
      offset: 0,
    } as never)
    render(<RunMetrics runId={RUN_ID} />, { wrapper })
    expect(
      await screen.findByText('Нет точек данных для выбранных метрик')
    ).toBeInTheDocument()
    expect(screen.getAllByText('—')).toHaveLength(5)
  })

  it('uses "lines" mode for long raw series', async () => {
    vi.mocked(metricsApi.summary).mockResolvedValue({
      run_id: RUN_ID,
      metrics: [item('long', 300)],
    } as never)
    vi.mocked(metricsApi.list).mockResolvedValue({
      run_id: RUN_ID,
      series: [
        {
          name: 'long',
          points: Array.from({ length: 300 }, (_, i) => ({
            step: i,
            value: i,
            timestamp: '2024-01-01T00:00:00Z',
          })),
        },
      ],
      total: 300,
      limit: 10000,
      offset: 0,
    } as never)
    render(<RunMetrics runId={RUN_ID} />, { wrapper })
    await vi.waitFor(() => expect(Plotly.react).toHaveBeenCalled())
    const traces = vi.mocked(Plotly.react).mock.calls.at(-1)?.[1] as PlotlyTrace[]
    expect(traces[0].mode).toBe('lines')
    expect(screen.queryByText(/Агрегированные данные/)).not.toBeInTheDocument()
  })

  it('uses aggregations for large data, shows loading then notice', async () => {
    vi.mocked(metricsApi.summary).mockResolvedValue({
      run_id: RUN_ID,
      metrics: [item('big', 9000), item('other', 2000)],
    } as never)
    let resolveAgg: (v: unknown) => void = () => {}
    vi.mocked(metricsApi.aggregations).mockReturnValue(
      new Promise((r) => {
        resolveAgg = r
      }) as never
    )
    render(<RunMetrics runId={RUN_ID} />, { wrapper })
    expect(await screen.findByText('Загрузка данных...')).toBeInTheDocument()
    expect(metricsApi.aggregations).toHaveBeenCalledWith(RUN_ID, {
      names: 'big,other',
      bucket_size: 18,
    })
    expect(metricsApi.list).not.toHaveBeenCalled()
    await act(async () => {
      resolveAgg({
        run_id: RUN_ID,
        bucket_size: 18,
        series: [
          {
            name: 'big',
            buckets: [
              { step_from: 0, step_to: 17, count: 18, avg: 1, min: 0, max: 2 },
              { step_from: 18, step_to: 35, count: 18, avg: 2, min: 1, max: 3 },
            ],
          },
        ],
      })
    })
    expect(await screen.findByText(/Агрегированные данные/)).toBeInTheDocument()
    const traces = vi.mocked(Plotly.react).mock.calls.at(-1)?.[1] as PlotlyTrace[]
    expect(traces[0]).toMatchObject({ mode: 'lines', x: [0, 18] })
    expect(traces[1].x).toEqual([])
  })
})
