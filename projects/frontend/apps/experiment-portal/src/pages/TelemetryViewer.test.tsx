import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { MemoryRouter } from 'react-router-dom'
import { QueryClientProvider, QueryClient } from '@tanstack/react-query'
import TelemetryViewer from './TelemetryViewer'
import { projectsApi, sensorsApi, experimentsApi, runsApi, captureSessionsApi } from '../api/client'
import { setActiveProjectId } from '../utils/activeProject'

vi.mock('../api/client', () => ({
  projectsApi: { list: vi.fn() },
  sensorsApi: { list: vi.fn() },
  experimentsApi: { list: vi.fn() },
  runsApi: { list: vi.fn() },
  captureSessionsApi: { list: vi.fn() },
}))
vi.mock('../utils/activeProject', () => ({ setActiveProjectId: vi.fn(), getActiveProjectId: vi.fn() }))
vi.mock('../utils/uuid', () => ({ generateUUID: () => 'new-panel' }))

const history = {
  captureSessionId: '',
  setCaptureSessionId: vi.fn(),
  effectiveSensorIds: [] as string[],
  valueMode: 'physical',
  includeLate: true,
  useAggregated: false,
  lastTimestamp: null as string | null,
  cursorBySensor: {} as Record<string, unknown>,
}
vi.mock('../hooks/useTelemetryHistory', () => ({ default: () => history }))

const live = {
  panelIds: ['p1'],
  panelsWrapRef: { current: null },
  panelsWrapWidth: 0,
  panelSizes: {},
  draggingPanelId: null,
  dragOverPanelId: null,
  recentValuesBySensor: {},
  addPanel: vi.fn(),
  removePanel: vi.fn(),
  movePanel: vi.fn(),
  onSizeChange: vi.fn(),
  onRecordReceived: vi.fn(),
  setDraggingPanelId: vi.fn(),
  setDragOverPanelId: vi.fn(),
  setPanelIds: vi.fn(),
}
vi.mock('../hooks/useLiveTelemetryWorkspace', () => ({ default: () => live }))

vi.mock('../components/telemetry/TelemetryFilters', () => ({
  default: (p: any) => (
    <div data-testid="filters">
      <span>{`${p.projectId}|${p.experimentId}|${p.runId}|${p.viewMode}`}</span>
      <button onClick={() => p.onProjectChange('p2')}>pick-project</button>
      <button onClick={() => p.onExperimentChange('e1')}>pick-exp</button>
      <button onClick={() => p.onRunChange('r1')}>pick-run</button>
      <button onClick={() => p.onViewModeChange('history')}>to-history</button>
      <button onClick={() => p.onViewModeChange('live')}>to-live</button>
    </div>
  ),
}))
vi.mock('../components/telemetry/LiveTelemetryWorkspace', () => ({
  default: (p: any) => (
    <div data-testid="live">
      <span>{`sensors=${p.sensors.length};sessions=${p.sessions.length};project=${p.projectName}`}</span>
      <button onClick={() => p.onAddHistorySession('cs-9')}>open-history</button>
    </div>
  ),
}))
vi.mock('../components/telemetry/TelemetryHistoryPanel', () => ({
  default: (p: any) => (
    <div data-testid="history">
      <span>{`session=${p.selectedSession?.id ?? 'none'}`}</span>
      <button onClick={p.onContinueLive}>continue-live</button>
      <button onClick={p.onExport}>export</button>
    </div>
  ),
}))
vi.mock('../components/TelemetryExportModal', () => ({
  default: (p: any) => (
    <div data-testid="export">
      {`${p.runId}|${p.sessionId}|${p.sessionOrdinal}|${p.initialSensorId}|${p.initialRawOrPhysical}`}
      <button onClick={p.onClose}>close-export</button>
    </div>
  ),
}))

function renderPage() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } })
  return render(
    <QueryClientProvider client={qc}>
      <MemoryRouter>
        <TelemetryViewer />
      </MemoryRouter>
    </QueryClientProvider>
  )
}

const projects = [{ id: 'p1', name: 'Main' }, { id: 'p2', name: 'Second' }]

describe('TelemetryViewer', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    window.localStorage.clear()
    Object.assign(history, { captureSessionId: '', effectiveSensorIds: [], lastTimestamp: null, cursorBySensor: {}, valueMode: 'physical' })
    vi.mocked(projectsApi.list).mockResolvedValue({ projects } as never)
    vi.mocked(sensorsApi.list).mockResolvedValue({ sensors: [{ id: 's1' }], total: 1 } as never)
    vi.mocked(experimentsApi.list).mockResolvedValue({ experiments: [{ id: 'e1' }] } as never)
    vi.mocked(runsApi.list).mockResolvedValue({ runs: [{ id: 'r1' }] } as never)
    vi.mocked(captureSessionsApi.list).mockResolvedValue({ capture_sessions: [{ id: 'cs1', ordinal_number: 3 }] } as never)
  })

  it('shows a loader, then selects the first project when none is stored', async () => {
    renderPage()
    expect(screen.getByText('Загрузка проектов...')).toBeInTheDocument()
    expect(await screen.findByTestId('filters')).toBeInTheDocument()
    await waitFor(() => expect(setActiveProjectId).toHaveBeenCalledWith('p1'))
    expect(await screen.findByText('p1|||live')).toBeInTheDocument()
  })

  it('shows an empty state without projects', async () => {
    vi.mocked(projectsApi.list).mockResolvedValue({ projects: [] } as never)
    renderPage()
    expect(await screen.findByText(/У вас нет проектов/)).toBeInTheDocument()
    expect(screen.queryByTestId('filters')).not.toBeInTheDocument()
  })

  it('keeps a stored project and loads dependent lists', async () => {
    window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p2', experimentId: 'e1', runId: 'r1', viewMode: 'live' }))
    renderPage()
    expect(await screen.findByText('p2|e1|r1|live')).toBeInTheDocument()
    expect(await screen.findByText('sensors=1;sessions=1;project=Second')).toBeInTheDocument()
    expect(experimentsApi.list).toHaveBeenCalledWith({ project_id: 'p2', page_size: 100 })
    expect(runsApi.list).toHaveBeenCalledWith('e1', { page_size: 100 })
    expect(captureSessionsApi.list).toHaveBeenCalledWith('r1', { page_size: 200 })
  })

  it('pages through every sensor of the project', async () => {
    window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', viewMode: 'live' }))
    const page = (n: number, start: number) => Array.from({ length: n }, (_, i) => ({ id: `s${start + i}` }))
    vi.mocked(sensorsApi.list)
      .mockResolvedValueOnce({ sensors: page(100, 0), total: 150 } as never)
      .mockResolvedValueOnce({ sensors: page(50, 100), total: 150 } as never)
    renderPage()
    expect(await screen.findByText('sensors=150;sessions=0;project=Main')).toBeInTheDocument()
    expect(vi.mocked(sensorsApi.list).mock.calls.map((c) => c[0])).toEqual([
      { project_id: 'p1', limit: 100, offset: 0 },
      { project_id: 'p1', limit: 100, offset: 100 },
    ])
  })

  it('stops paging once total is reached even with a full page', async () => {
    window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', viewMode: 'live' }))
    vi.mocked(sensorsApi.list).mockResolvedValue({ sensors: Array.from({ length: 100 }, (_, i) => ({ id: `s${i}` })), total: 100 } as never)
    renderPage()
    expect(await screen.findByText('sensors=100;sessions=0;project=Main')).toBeInTheDocument()
    expect(sensorsApi.list).toHaveBeenCalledTimes(1)
  })

  it('resets selections when the stored experiment or run no longer exists', async () => {
    window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', experimentId: 'gone', runId: 'gone-run', viewMode: 'live' }))
    renderPage()
    expect(await screen.findByText('p1|||live')).toBeInTheDocument()
  })

  it('resets the run when it is missing from the experiment', async () => {
    window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', experimentId: 'e1', runId: 'gone-run', viewMode: 'live' }))
    renderPage()
    expect(await screen.findByText('p1|e1||live')).toBeInTheDocument()
  })

  it('cascades filter changes', async () => {
    const user = userEvent.setup()
    window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', viewMode: 'live' }))
    renderPage()
    await screen.findByText('p1|||live')
    await user.click(screen.getByText('pick-exp'))
    expect(await screen.findByText('p1|e1||live')).toBeInTheDocument()
    await user.click(screen.getByText('pick-run'))
    expect(await screen.findByText('p1|e1|r1|live')).toBeInTheDocument()
    await user.click(screen.getByText('pick-project'))
    expect(await screen.findByText('p2|||live')).toBeInTheDocument()
    expect(setActiveProjectId).toHaveBeenLastCalledWith('p2')
  })

  it('enables the add-panel button only when sensors are available', async () => {
    const user = userEvent.setup()
    window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', viewMode: 'live' }))
    renderPage()
    const fab = await screen.findByRole('button', { name: 'Добавить панель' })
    await waitFor(() => expect(fab).toBeEnabled())
    await user.click(fab)
    expect(live.addPanel).toHaveBeenCalled()
  })

  it('disables the add-panel button when the project has no sensors', async () => {
    window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', viewMode: 'live' }))
    vi.mocked(sensorsApi.list).mockResolvedValue({ sensors: [], total: 0 } as never)
    renderPage()
    const fab = await screen.findByRole('button', { name: 'Добавить панель' })
    await screen.findByText('sensors=0;sessions=0;project=Main')
    expect(fab).toBeDisabled()
  })

  it('opens the history for a session from the live view', async () => {
    const user = userEvent.setup()
    window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', viewMode: 'live' }))
    renderPage()
    await user.click(await screen.findByText('open-history'))
    expect(history.setCaptureSessionId).toHaveBeenCalledWith('cs-9')
    expect(await screen.findByTestId('history')).toBeInTheDocument()
    expect(screen.queryByRole('button', { name: 'Добавить панель' })).not.toBeInTheDocument()
  })

  it('selects the first capture session in history mode when none matches', async () => {
    window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', experimentId: 'e1', runId: 'r1', viewMode: 'history' }))
    renderPage()
    await waitFor(() => expect(history.setCaptureSessionId).toHaveBeenCalledWith('cs1'))
  })

  it('does not reselect when the current session is valid, and passes it to the panel', async () => {
    history.captureSessionId = 'cs1'
    window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', experimentId: 'e1', runId: 'r1', viewMode: 'history' }))
    renderPage()
    expect(await screen.findByText('session=cs1')).toBeInTheDocument()
    expect(history.setCaptureSessionId).not.toHaveBeenCalled()
  })

  describe('continue live', () => {
    beforeEach(() => {
      window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', experimentId: 'e1', runId: 'r1', viewMode: 'history' }))
    })

    it('does nothing without sensors in the history', async () => {
      const user = userEvent.setup()
      renderPage()
      await user.click(await screen.findByText('continue-live'))
      expect(live.setPanelIds).not.toHaveBeenCalled()
    })

    it('creates a live panel seeded from the history and switches mode', async () => {
      const user = userEvent.setup()
      Object.assign(history, {
        effectiveSensorIds: ['s1'], lastTimestamp: '2026-01-01T00:00:00Z', valueMode: 'raw',
        cursorBySensor: { s1: { timestamp: 't', id: 4 } },
      })
      renderPage()
      await user.click(await screen.findByText('continue-live'))
      const saved = JSON.parse(window.localStorage.getItem('telemetry_panel_state_new-panel')!)
      expect(saved).toMatchObject({
        title: 'History -> Live', selectedSensorIds: ['s1'], valueMode: 'raw', maxPoints: 500,
        timeWindowSeconds: 300, useLatestAnchor: true, startFromTimestamp: '2026-01-01T00:00:00Z',
        startFromCursorBySensor: { s1: { timestamp: 't', id: 4 } },
      })
      expect(live.setPanelIds).toHaveBeenCalled()
      const updater = live.setPanelIds.mock.calls[0][0] as (v: string[]) => string[]
      expect(updater(['a'])).toEqual(['a', 'new-panel'])
      expect(await screen.findByTestId('live')).toBeInTheDocument()
    })

    it('omits optional anchors when the history has none', async () => {
      const user = userEvent.setup()
      history.effectiveSensorIds = ['s1']
      renderPage()
      await user.click(await screen.findByText('continue-live'))
      const saved = JSON.parse(window.localStorage.getItem('telemetry_panel_state_new-panel')!)
      expect(saved.startFromTimestamp).toBeUndefined()
      expect(saved.startFromCursorBySensor).toBeUndefined()
    })
  })

  describe('export', () => {
    it('opens the export modal for the run and closes it', async () => {
      const user = userEvent.setup()
      Object.assign(history, { captureSessionId: 'cs1', effectiveSensorIds: ['s1'], valueMode: 'raw' })
      window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', experimentId: 'e1', runId: 'r1', viewMode: 'history' }))
      renderPage()
      await screen.findByText('session=cs1')
      await user.click(screen.getByText('export'))
      expect(await screen.findByTestId('export')).toHaveTextContent('r1|cs1|3|s1|raw')
      await user.click(screen.getByText('close-export'))
      expect(screen.queryByTestId('export')).not.toBeInTheDocument()
    })

    it('does not preselect a sensor when several are active, and requires a run', async () => {
      const user = userEvent.setup()
      Object.assign(history, { captureSessionId: 'cs1', effectiveSensorIds: ['s1', 's2'] })
      window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', experimentId: 'e1', runId: 'r1', viewMode: 'history' }))
      renderPage()
      await screen.findByText('session=cs1')
      await user.click(screen.getByText('export'))
      expect(await screen.findByTestId('export')).toHaveTextContent('r1|cs1|3||physical')
    })

    it('does not render the modal when no run is selected', async () => {
      const user = userEvent.setup()
      window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId: 'p1', viewMode: 'history' }))
      renderPage()
      await user.click(await screen.findByText('export'))
      expect(screen.queryByTestId('export')).not.toBeInTheDocument()
    })
  })
})
