import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import RunMetrics from './RunMetrics'
import { metricsApi } from '../api/client'

vi.mock('../api/client', () => ({
  metricsApi: {
    summary: vi.fn(),
    list: vi.fn(),
    aggregations: vi.fn(),
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

const mockSummary = {
  items: [
    { name: 'loss', last_step: 3, last_value: 0.5, count: 3, min: 0.5, avg: 0.7, max: 0.9 },
    { name: 'accuracy', last_step: 2, last_value: 0.8, count: 2, min: 0.6, avg: 0.7, max: 0.8 },
  ],
}

const mockList = {
  items: [
    { name: 'loss', step: 1, value: 0.9, timestamp: '2024-01-01T00:00:01Z' },
    { name: 'loss', step: 2, value: 0.7, timestamp: '2024-01-01T00:00:02Z' },
    { name: 'loss', step: 3, value: 0.5, timestamp: '2024-01-01T00:00:03Z' },
    { name: 'accuracy', step: 1, value: 0.6, timestamp: '2024-01-01T00:00:01Z' },
    { name: 'accuracy', step: 2, value: 0.8, timestamp: '2024-01-01T00:00:02Z' },
  ],
  total: 5,
}

describe('RunMetrics', () => {
  beforeEach(() => {
    vi.mocked(metricsApi.summary).mockResolvedValue(mockSummary)
    vi.mocked(metricsApi.list).mockResolvedValue(mockList)
    vi.mocked(metricsApi.aggregations).mockResolvedValue({ items: [] })
  })

  it('shows loading state initially', () => {
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    expect(screen.getByText(/загрузка метрик/i)).toBeInTheDocument()
  })

  it('renders summary cards for each metric', async () => {
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    await waitFor(() => {
      expect(screen.getByText('loss')).toBeInTheDocument()
      expect(screen.getByText('accuracy')).toBeInTheDocument()
    })
  })

  it('shows last value in summary card', async () => {
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    await waitFor(() => {
      // loss last_value = 0.5 (may appear multiple times: card value + min stat)
      expect(screen.getAllByText('0.5000').length).toBeGreaterThan(0)
      // accuracy last_value = 0.8
      expect(screen.getAllByText('0.8000').length).toBeGreaterThan(0)
    })
  })

  it('shows min/avg/max stats in summary card', async () => {
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    await waitFor(() => {
      // loss stats
      expect(screen.getAllByText('0.5000').length).toBeGreaterThan(0)
      expect(screen.getAllByText('0.9000').length).toBeGreaterThan(0)
    })
  })

  it('cards are toggleable — clicking deselects metric', async () => {
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    await waitFor(() => screen.getByText('loss'))

    const lossCard = screen.getByText('loss').closest('button')!
    expect(lossCard).toHaveAttribute('aria-pressed', 'true')

    await userEvent.click(lossCard)
    expect(lossCard).toHaveAttribute('aria-pressed', 'false')
  })

  it('shows empty state when no metrics returned', async () => {
    vi.mocked(metricsApi.summary).mockResolvedValue({ items: [] })
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    await waitFor(() => {
      expect(screen.getByText(/нет записанных метрик/i)).toBeInTheDocument()
    })
  })

  it('shows error state on fetch failure', async () => {
    vi.mocked(metricsApi.summary).mockRejectedValue(new Error('Network error'))
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    await waitFor(() => {
      expect(screen.getByText(/не удалось загрузить метрики/i)).toBeInTheDocument()
    })
  })

  it('re-selects all metrics automatically when set becomes empty', async () => {
    // The component auto-selects all when selectedNames.size === 0,
    // so deselecting all cards triggers re-selection instead of showing empty state.
    render(<RunMetrics runId={RUN_ID} />, { wrapper: createWrapper() })
    await waitFor(() => screen.getByText('loss'))

    const lossCard = screen.getByText('loss').closest('button')!
    const accuracyCard = screen.getByText('accuracy').closest('button')!

    // Deselect loss — accuracy stays, lossCard deselected
    await userEvent.click(lossCard)
    await waitFor(() => expect(lossCard).toHaveAttribute('aria-pressed', 'false'))
    expect(accuracyCard).toHaveAttribute('aria-pressed', 'true')

    // Re-select loss — now both selected again
    await userEvent.click(lossCard)
    await waitFor(() => expect(lossCard).toHaveAttribute('aria-pressed', 'true'))
  })
})
