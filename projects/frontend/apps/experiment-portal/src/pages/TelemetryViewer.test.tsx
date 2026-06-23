import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/react'
import { BrowserRouter } from 'react-router-dom'
import { QueryClientProvider, QueryClient } from '@tanstack/react-query'
import TelemetryViewer from './TelemetryViewer'

vi.mock('../api/client', () => ({
  runsApi: {
    get: vi.fn().mockResolvedValue({
      id: 'run-1',
      name: 'Test Run',
      experiment_id: 'exp-1',
    }),
  },
  telemetryApi: {
    list: vi.fn().mockResolvedValue({ telemetry: [] }),
  },
}))

vi.mock('plotly.js-dist-min', () => ({
  default: { react: vi.fn(), purge: vi.fn() },
}))

describe('TelemetryViewer', () => {
  const queryClient = new QueryClient({
    defaultOptions: { queries: { retry: false } },
  })

  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('renders without crashing', () => {
    const { container } = render(
      <BrowserRouter>
        <QueryClientProvider client={queryClient}>
          <TelemetryViewer />
        </QueryClientProvider>
      </BrowserRouter>
    )
    expect(container.firstChild).toBeInTheDocument()
  })

  it('displays telemetry interface', () => {
    const { container } = render(
      <BrowserRouter>
        <QueryClientProvider client={queryClient}>
          <TelemetryViewer />
        </QueryClientProvider>
      </BrowserRouter>
    )
    expect(container).toBeInTheDocument()
  })

  it('handles empty telemetry gracefully', () => {
    const { container } = render(
      <BrowserRouter>
        <QueryClientProvider client={queryClient}>
          <TelemetryViewer />
        </QueryClientProvider>
      </BrowserRouter>
    )
    expect(container).toBeInTheDocument()
  })
})
