import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { MemoryRouter, Route, Routes, useLocation } from 'react-router-dom'
import { QueryClientProvider, QueryClient } from '@tanstack/react-query'
import Plotly from 'plotly.js-dist-min'
import ComparisonPage from './ComparisonPage'
import { runsApi, comparisonApi } from '../api/client'

vi.mock('../api/client', () => ({
  runsApi: { list: vi.fn() },
  comparisonApi: { compare: vi.fn(), exportUrl: vi.fn() },
}))
vi.mock('plotly.js-dist-min', () => ({
  default: { react: vi.fn(), purge: vi.fn() },
}))
vi.mock('../utils/notify', () => ({
  notifyError: vi.fn(), notifySuccess: vi.fn(), notifyInfo: vi.fn(), notifyWarning: vi.fn(),
}))

function Location() {
  return <div data-testid="loc">{useLocation().pathname}</div>
}

function renderPage() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } })
  return render(
    <QueryClientProvider client={qc}>
      <MemoryRouter initialEntries={['/experiments/e1/compare']}>
        <Routes>
          <Route path="/experiments/:experimentId/compare" element={<ComparisonPage />} />
          <Route path="*" element={<Location />} />
        </Routes>
      </MemoryRouter>
    </QueryClientProvider>
  )
}

const runs = [
  { id: 'r1', name: 'Run one', status: 'succeeded' },
  { id: 'r2', name: 'Run two', status: 'running' },
]

function entry(id: string, name: string, metrics: Record<string, unknown>) {
  return { run_id: id, run_name: name, status: 'succeeded', metrics }
}

const result = {
  metric_names: ['loss', 'acc'],
  runs: [
    entry('r1', 'Run one', {
      loss: { summary: { last_value: 0.5, min: 0.25, max: 3 }, series: [{ step: 0, value: 3 }, { step: 1, value: 0.5 }] },
    }),
    entry('r2', 'Run two', {
      acc: { summary: { last_value: null, min: undefined, max: 1 }, series: [] },
    }),
  ],
}

describe('ComparisonPage', () => {
  let openSpy: ReturnType<typeof vi.spyOn>

  beforeEach(() => {
    vi.clearAllMocks()
    openSpy = vi.spyOn(window, 'open').mockImplementation(() => null)
    vi.mocked(runsApi.list).mockResolvedValue({ runs } as never)
    vi.mocked(comparisonApi.exportUrl).mockImplementation((_e, p) => `/export?f=${p.format}&n=${p.metric_names.join('+')}`)
  })
  afterEach(() => openSpy.mockRestore())

  it('shows a spinner while loading runs, then the run list and the hint', async () => {
    renderPage()
    expect(screen.getByRole('progressbar')).toBeInTheDocument()
    expect(await screen.findByText('Run one')).toBeInTheDocument()
    expect(screen.getByText('running')).toBeInTheDocument()
    expect(screen.getByText(/Выберите runs и нажмите/)).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Compare' })).toBeDisabled()
    expect(screen.getByText('Выберите хотя бы один run')).toBeInTheDocument()
    expect(runsApi.list).toHaveBeenCalledWith('e1', { page_size: 100 })
  })

  it('shows an empty state when there are no runs', async () => {
    vi.mocked(runsApi.list).mockResolvedValue({ runs: [] } as never)
    renderPage()
    expect(await screen.findByText('Нет доступных runs')).toBeInTheDocument()
  })

  it('goes back to the experiment', async () => {
    const user = userEvent.setup()
    renderPage()
    await user.click(await screen.findByRole('button', { name: /Назад/ }))
    expect(screen.getByTestId('loc')).toHaveTextContent('/experiments/e1')
  })

  it('compares selected runs, renders summary and charts, and can toggle a run off', async () => {
    const user = userEvent.setup()
    vi.mocked(comparisonApi.compare).mockResolvedValue(result as never)
    renderPage()
    await user.click(await screen.findByRole('checkbox', { name: /Run one/ }))
    await user.click(screen.getByRole('checkbox', { name: /Run two/ }))
    await user.click(screen.getByRole('checkbox', { name: /Run two/ }))
    expect(screen.getByRole('checkbox', { name: /Run two/ })).not.toBeChecked()
    await user.click(screen.getByRole('button', { name: 'Compare' }))

    await waitFor(() => expect(comparisonApi.compare).toHaveBeenCalled())
    expect(vi.mocked(comparisonApi.compare).mock.calls[0]).toEqual(['e1', { run_ids: ['r1'], metric_names: [] }])

    expect(await screen.findByText('Summary')).toBeInTheDocument()
    const table = screen.getByRole('table')
    expect(within(table).getByText('0.5000')).toBeInTheDocument()
    expect(within(table).getByText('0.2500')).toBeInTheDocument()
    expect(within(table).getByText('3')).toBeInTheDocument()
    expect(within(table).getAllByText('—').length).toBeGreaterThan(0)
    await waitFor(() => expect(Plotly.react).toHaveBeenCalledTimes(2))
    const [, traces1] = vi.mocked(Plotly.react).mock.calls[0] as unknown as [unknown, Array<{ mode: string; name: string }>]
    expect(traces1).toHaveLength(1)
    expect(traces1[0]).toMatchObject({ name: 'Run one', mode: 'lines+markers' })
    const [, traces2] = vi.mocked(Plotly.react).mock.calls[1] as unknown as [unknown, unknown[]]
    expect(traces2).toHaveLength(0)
  })

  it('uses plain lines for long series', async () => {
    const user = userEvent.setup()
    const series = Array.from({ length: 201 }, (_, i) => ({ step: i, value: i }))
    vi.mocked(comparisonApi.compare).mockResolvedValue({
      metric_names: ['loss'],
      runs: [entry('unknown-run', 'Ghost', { loss: { summary: {}, series } })],
    } as never)
    renderPage()
    await user.click(await screen.findByRole('checkbox', { name: /Run one/ }))
    await user.click(screen.getByRole('button', { name: 'Compare' }))
    await waitFor(() => expect(Plotly.react).toHaveBeenCalled())
    const [, traces] = vi.mocked(Plotly.react).mock.calls[0] as unknown as [unknown, Array<{ mode: string; line: { color: string } }>]
    expect(traces[0].mode).toBe('lines')
    expect(traces[0].line.color).toBe('#636EFA')
  })

  it('selects all metrics after the first compare and lets the user narrow them', async () => {
    const user = userEvent.setup()
    vi.mocked(comparisonApi.compare).mockResolvedValue(result as never)
    renderPage()
    await user.click(await screen.findByRole('checkbox', { name: /Run one/ }))
    await user.click(screen.getByRole('button', { name: 'Compare' }))
    await screen.findByText('Summary')

    const select = screen.getByRole('combobox')
    expect(within(select).getByText('loss')).toBeInTheDocument()
    expect(within(select).getByText('acc')).toBeInTheDocument()

    await user.click(select)
    await user.click(await screen.findByRole('option', { name: 'acc' }))
    await user.keyboard('{Escape}')
    await waitFor(() => expect(within(screen.getByRole('combobox')).queryByText('acc')).not.toBeInTheDocument())
    // Only "loss" remains in the table header
    expect(within(screen.getByRole('table')).queryByText('acc')).not.toBeInTheDocument()
    expect(within(screen.getByRole('table')).getByText('loss')).toBeInTheDocument()

    // re-compare now sends the narrowed metric list and keeps the selection
    vi.mocked(comparisonApi.compare).mockResolvedValue({ ...result, runs: [...result.runs] } as never)
    await user.click(screen.getByRole('button', { name: 'Compare' }))
    await waitFor(() => expect(comparisonApi.compare).toHaveBeenCalledTimes(2))
    await waitFor(() => expect(Plotly.purge).toHaveBeenCalled())
    expect(vi.mocked(comparisonApi.compare).mock.calls[1][1]).toEqual({ run_ids: ['r1'], metric_names: ['loss'] })
  })

  it('exports CSV and JSON with the selected metrics', async () => {
    const user = userEvent.setup()
    vi.mocked(comparisonApi.compare).mockResolvedValue(result as never)
    renderPage()
    await user.click(await screen.findByRole('checkbox', { name: /Run one/ }))
    await user.click(screen.getByRole('button', { name: 'Compare' }))
    await user.click(await screen.findByRole('button', { name: 'Export CSV' }))
    expect(openSpy).toHaveBeenLastCalledWith('/export?f=csv&n=loss+acc', '_blank')
    await user.click(screen.getByRole('button', { name: 'Export JSON' }))
    expect(openSpy).toHaveBeenLastCalledWith('/export?f=json&n=loss+acc', '_blank')
  })

  it('exports with all metric names when the selection is cleared', async () => {
    const user = userEvent.setup()
    vi.mocked(comparisonApi.compare).mockResolvedValue(result as never)
    renderPage()
    await user.click(await screen.findByRole('checkbox', { name: /Run one/ }))
    await user.click(screen.getByRole('button', { name: 'Compare' }))
    await screen.findByText('Summary')
    await user.click(screen.getByRole('combobox'))
    await user.click(await screen.findByRole('option', { name: 'loss' }))
    await user.click(await screen.findByRole('option', { name: 'acc' }))
    await user.keyboard('{Escape}')
    // nothing selected: table and charts fall back to every metric
    expect(within(screen.getByRole('table')).getByText('acc')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'Export CSV' }))
    expect(openSpy).toHaveBeenLastCalledWith('/export?f=csv&n=loss+acc', '_blank')
    await user.click(screen.getByRole('button', { name: 'Export JSON' }))
    expect(openSpy).toHaveBeenLastCalledWith('/export?f=json&n=loss+acc', '_blank')
  })

  it('shows an error when the comparison fails', async () => {
    const user = userEvent.setup()
    vi.mocked(comparisonApi.compare).mockRejectedValue(new Error('boom'))
    renderPage()
    await user.click(await screen.findByRole('checkbox', { name: /Run one/ }))
    await user.click(screen.getByRole('button', { name: 'Compare' }))
    expect(await screen.findByText('Не удалось загрузить сравнение')).toBeInTheDocument()
    expect(screen.queryByText('Summary')).not.toBeInTheDocument()
  })
})
