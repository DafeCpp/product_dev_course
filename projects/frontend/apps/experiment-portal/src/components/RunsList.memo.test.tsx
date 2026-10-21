import { describe, it, expect, vi } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import RunsList from './RunsList'
import { runsApi } from '../api/client'
import { createMockRun } from '../testUtils/factories'

vi.mock('../api/client', () => ({
  runsApi: { list: vi.fn(), exportData: vi.fn() },
}))

// Tags renders once per row, so its call count is the number of rows rendered.
const tagsRenders = vi.hoisted(() => ({ count: 0 }))
vi.mock('./common/Tags', () => ({
  default: () => {
    tagsRenders.count++
    return null
  },
}))

describe('RunsList row memoization', () => {
  it('re-renders only the toggled row when one run is selected', async () => {
    const user = userEvent.setup()
    const runs = ['r1', 'r2', 'r3'].map((id) => createMockRun({ id, name: id }))
    vi.mocked(runsApi.list).mockResolvedValue({ runs, total: 3, page: 1, page_size: 20 } as never)
    const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } })
    render(
      <QueryClientProvider client={qc}>
        <MemoryRouter>
          <RunsList experimentId="exp-1" />
        </MemoryRouter>
      </QueryClientProvider>
    )
    await screen.findByText('r1')

    const before = tagsRenders.count
    await user.click(screen.getByRole('checkbox', { name: 'select run r2' }))
    expect(screen.getByRole('checkbox', { name: 'select run r2' })).toBeChecked()
    expect(tagsRenders.count - before).toBe(1)
  })
})
