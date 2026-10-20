import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { act, render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import type { ReactNode } from 'react'
import RunsList from './RunsList'
import { runsApi } from '../api/client'
import { downloadBlob } from '../utils/download'
import { notifyError, notifySuccess } from '../utils/notify'

vi.mock('../api/client', () => ({
  runsApi: {
    list: vi.fn(),
    bulkTags: vi.fn(),
    exportData: vi.fn(),
  },
}))
vi.mock('../utils/download', () => ({ downloadBlob: vi.fn() }))
vi.mock('../utils/notify', () => ({ notifyError: vi.fn(), notifySuccess: vi.fn() }))

const wrapper = ({ children }: { children: ReactNode }) => {
  const queryClient = new QueryClient({
    defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
  })
  return (
    <QueryClientProvider client={queryClient}>
      <MemoryRouter>{children}</MemoryRouter>
    </QueryClientProvider>
  )
}

const baseRun = {
  id: 'run-1',
  experiment_id: 'exp-1',
  project_id: 'p1',
  created_by: 'u1',
  name: 'Run One',
  git_sha: null,
  env: null,
  notes: null,
  params: { a: 1 },
  status: 'running' as const,
  tags: null as unknown as string[],
  started_at: null,
  finished_at: null,
  duration_seconds: 125,
  metadata: {},
  auto_complete_after_minutes: null,
  created_at: '2024-01-01T00:00:00Z',
  updated_at: '2024-01-01T00:00:00Z',
}

function setupRuns() {
  vi.mocked(runsApi.list).mockResolvedValue({
    runs: [baseRun, { ...baseRun, id: 'run-2', name: 'Run Two', duration_seconds: 5 }],
    total: 2,
    page: 1,
    page_size: 20,
  } as never)
}

describe('RunsList branches', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    setupRuns()
  })

  it('formats minute and second durations and missing dates/tags', async () => {
    render(<RunsList experimentId="exp-1" />, { wrapper })
    expect(await screen.findByText('Run One')).toBeInTheDocument()
    expect(screen.getByText('2м 5с')).toBeInTheDocument()
    expect(screen.getByText('5с')).toBeInTheDocument()
  })

  it('selects and deselects all runs', async () => {
    const user = userEvent.setup()
    render(<RunsList experimentId="exp-1" />, { wrapper })
    const all = await screen.findByLabelText('select all runs')
    await user.click(all)
    expect(screen.getByRole('button', { name: /bulk tagging \(2\)/i })).toBeEnabled()
    expect(all).toBeChecked()
    await user.click(all)
    expect(screen.getByRole('button', { name: /bulk tagging \(0\)/i })).toBeDisabled()
  })

  it('exports CSV and shows exporting state', async () => {
    const user = userEvent.setup()
    let resolve: (v: Blob) => void = () => {}
    vi.mocked(runsApi.exportData).mockReturnValueOnce(
      new Promise<Blob>((r) => {
        resolve = r
      }) as never
    )
    render(<RunsList experimentId="exp-1" />, { wrapper })
    await user.click(await screen.findByRole('button', { name: 'Экспорт CSV' }))
    expect(screen.getByRole('button', { name: 'Экспорт...' })).toBeDisabled()
    const blob = new Blob(['x'])
    await act(async () => {
      resolve(blob)
    })
    expect(runsApi.exportData).toHaveBeenCalledWith('exp-1', { format: 'csv' })
    expect(downloadBlob).toHaveBeenCalledWith(blob, 'runs.csv', 'text/csv')
    expect(notifySuccess).toHaveBeenCalledWith('CSV экспортирован')
    expect(screen.getByRole('button', { name: 'Экспорт CSV' })).toBeEnabled()
  })

  it('exports JSON', async () => {
    const user = userEvent.setup()
    const blob = new Blob(['{}'])
    vi.mocked(runsApi.exportData).mockResolvedValueOnce(blob as never)
    render(<RunsList experimentId="exp-1" />, { wrapper })
    await user.click(await screen.findByRole('button', { name: 'Экспорт JSON' }))
    expect(downloadBlob).toHaveBeenCalledWith(blob, 'runs.json', 'application/json')
    expect(notifySuccess).toHaveBeenCalledWith('JSON экспортирован')
  })

  it('notifies on export error', async () => {
    const user = userEvent.setup()
    vi.mocked(runsApi.exportData).mockRejectedValueOnce(new Error('x'))
    render(<RunsList experimentId="exp-1" />, { wrapper })
    await user.click(await screen.findByRole('button', { name: 'Экспорт JSON' }))
    expect(notifyError).toHaveBeenCalledWith('Ошибка экспорта JSON')
  })
})
