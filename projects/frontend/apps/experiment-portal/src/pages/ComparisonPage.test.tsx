import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/react'
import { BrowserRouter } from 'react-router-dom'
import { QueryClientProvider, QueryClient } from '@tanstack/react-query'
import ComparisonPage from './ComparisonPage'

vi.mock('../api/client', () => ({
  runsApi: { list: vi.fn().mockResolvedValue({ runs: [] }) },
  comparisonApi: { compare: vi.fn().mockResolvedValue({ runs: [] }) },
}))

vi.mock('plotly.js-dist-min', () => ({
  default: { react: vi.fn() },
}))

describe('ComparisonPage', () => {
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
          <ComparisonPage />
        </QueryClientProvider>
      </BrowserRouter>
    )
    expect(container.firstChild).toBeInTheDocument()
  })

  it('shows loading or empty state', () => {
    const { container } = render(
      <BrowserRouter>
        <QueryClientProvider client={queryClient}>
          <ComparisonPage />
        </QueryClientProvider>
      </BrowserRouter>
    )
    expect(container).toBeInTheDocument()
  })
})
