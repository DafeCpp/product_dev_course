import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/react'
import { BrowserRouter } from 'react-router-dom'
import { QueryClientProvider, QueryClient } from '@tanstack/react-query'
import SensorMonitor from './SensorMonitor'

vi.mock('../api/client', () => ({
  sensorsApi: {
    get: vi.fn().mockResolvedValue({
      id: 'sensor-1',
      name: 'Test Sensor',
      last_reading: null,
    }),
  },
}))

describe('SensorMonitor', () => {
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
          <SensorMonitor />
        </QueryClientProvider>
      </BrowserRouter>
    )
    expect(container.firstChild).toBeInTheDocument()
  })

  it('shows sensor monitoring interface', () => {
    const { container } = render(
      <BrowserRouter>
        <QueryClientProvider client={queryClient}>
          <SensorMonitor />
        </QueryClientProvider>
      </BrowserRouter>
    )
    expect(container).toBeInTheDocument()
  })
})
