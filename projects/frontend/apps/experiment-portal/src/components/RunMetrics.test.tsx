import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import RunMetrics from './RunMetrics'
import { metricsApi } from '../api/client'

vi.mock('../api/client', () => ({
  metricsApi: {
    query: vi.fn(),
  },
}))

// Plotly is heavy — mock it out in unit tests
vi.mock('plotly.js-dist-min', () => ({
  default: {
    react: vi.fn(),
    purge: vi.fn(),
  },
}))

function createWrapper() {
  const queryClient = new QueryClient({
    defaultOptions: { queries: { retry: false } },
  })
  return ({ children }: { children: React.ReactNode }) => (
    <QueryClientProvider client={queryClient}>{children}</QueryClientProvider>
  )
}

const RUN_ID = 'run-abc'

const mockSeries = [
  {
    name: 'loss',
    points: [
      { step: 1, value: 0.9, timestamp: '2024-01-01T00:00:01Z' },
      { step: 2, value: 0.7, timestamp: '2024-01-01T00:00:02Z' },
      { step: 3, value: 0.5, timestamp: '2024-01-01T00:00:03Z' },
    ],
  },
  {
    name: 'accuracy',
    points: [
      { step: 1, value: 0.6, timestamp: '2024-01-01T00:00:01Z' },
      { step: 2, value: 0.8, timestamp: '2024-01-01T00:00:02Z' },
    ],
  },
]

describe('RunMetrics', () => {
  beforeEach(() => {
    vi.mocked(metricsApi.query).mockResolvedValue({
      run_id: RUN_ID,
      series: mockSeries,
    })
  })

  it('shows loading state initially', () => {
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    expect(screen.getByText(/загрузка метрик/i)).toBeInTheDocument()
  })

  it('renders metric tabs when data loads', async () => {
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    await waitFor(() => {
      expect(screen.getByRole('tab', { name: /loss/i })).toBeInTheDocument()
      expect(screen.getByRole('tab', { name: /accuracy/i })).toBeInTheDocument()
    })
  })

  it('shows point count and last value summary', async () => {
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    await waitFor(() => {
      // loss tab is selected by default: 3 points, last step=3 value=0.5
      expect(screen.getByText(/3 точек/)).toBeInTheDocument()
      expect(screen.getByText('0.5')).toBeInTheDocument()
      // "3" appears multiple times (tab count + step) — just check summary text
      expect(screen.getByText(/на шаге/)).toBeInTheDocument()
    })
  })

  it('switches to another metric tab', async () => {
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    await waitFor(() => screen.getByRole('tab', { name: /accuracy/i }))

    await userEvent.click(screen.getByRole('tab', { name: /accuracy/i }))

    expect(screen.getByText(/2 точек/)).toBeInTheDocument()
    expect(screen.getByText('0.8')).toBeInTheDocument()
  })

  it('shows empty state when no series returned', async () => {
    vi.mocked(metricsApi.query).mockResolvedValue({ run_id: RUN_ID, series: [] })
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    await waitFor(() => {
      expect(screen.getByText(/метрики не записаны/i)).toBeInTheDocument()
    })
  })

  it('shows error state on fetch failure', async () => {
    vi.mocked(metricsApi.query).mockRejectedValue(new Error('Network error'))
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    await waitFor(() => {
      expect(screen.getByText(/не удалось загрузить метрики/i)).toBeInTheDocument()
    })
  })

  it('does not show tabs when only one series', async () => {
    vi.mocked(metricsApi.query).mockResolvedValue({
      run_id: RUN_ID,
      series: [mockSeries[0]],
    })
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    await waitFor(() => screen.getByText(/3 точек/))
    expect(screen.queryByRole('tablist')).not.toBeInTheDocument()
  })
})
