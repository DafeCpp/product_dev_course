import { describe, it, expect, vi, beforeEach } from 'vitest'
import type { ComponentType } from 'react'
import { render, screen } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { MemoryRouter, Route, Routes } from 'react-router-dom'
import ExperimentDetail from './ExperimentDetail'
import RunDetail from './RunDetail'
import SensorDetail from './SensorDetail'
import ComparisonPage from './ComparisonPage'
import * as client from '../api/client'

vi.mock('../api/client')

// A route without the :id segment, so useParams() yields no id.
function renderWithoutParam(Page: ComponentType) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } })
  return render(
    <QueryClientProvider client={qc}>
      <MemoryRouter initialEntries={['/broken']}>
        <Routes>
          <Route path="/broken" element={<Page />} />
        </Routes>
      </MemoryRouter>
    </QueryClientProvider>,
  )
}

const mockedApiFns = () =>
  (Object.values(client) as unknown[])
    .filter((api): api is Record<string, unknown> => typeof api === 'object' && api !== null)
    .flatMap((api) => Object.values(api))
    .filter((fn) => vi.isMockFunction(fn))

const apiCalls = () =>
  mockedApiFns().reduce((n, fn) => n + (fn as ReturnType<typeof vi.fn>).mock.calls.length, 0)

describe('detail pages without the required route param', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it.each([
    ['ExperimentDetail', ExperimentDetail],
    ['RunDetail', RunDetail],
    ['SensorDetail', SensorDetail],
    ['ComparisonPage', ComparisonPage],
  ])('%s shows an invalid-link message and sends no requests', (_name, Page) => {
    renderWithoutParam(Page)
    expect(screen.getByText('Некорректная ссылка')).toBeInTheDocument()
    expect(screen.getByRole('link', { name: 'К экспериментам' })).toHaveAttribute('href', '/experiments')
    expect(mockedApiFns().length).toBeGreaterThan(0)
    expect(apiCalls()).toBe(0)
  })
})
