import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { formatDateTimeSeconds } from '../utils/format'
import { MemoryRouter } from 'react-router-dom'
import { act, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import type { ReactNode } from 'react'
import RunDetail from './RunDetail'
import { runsApi, experimentsApi, captureSessionsApi, sensorsApi } from '../api/client'
import { useCountdown } from '../hooks/useCountdown'
import { setActiveProjectId } from '../utils/activeProject'
import {
  createMockRun,
  createMockExperiment,
  createMockCaptureSession,
  createMockSensor,
} from '../testUtils/factories'
import { pickMaterialSelectOption } from '../testUtils/materialSelect'
import type { CaptureSession, Experiment, Run } from '../types'

vi.mock('../api/client', () => ({
  runsApi: { get: vi.fn(), complete: vi.fn(), fail: vi.fn(), update: vi.fn() },
  experimentsApi: { get: vi.fn() },
  captureSessionsApi: { list: vi.fn(), create: vi.fn(), stop: vi.fn(), delete: vi.fn() },
  sensorsApi: { list: vi.fn() },
}))
vi.mock('../utils/notify', () => ({ notifyError: vi.fn(), notifySuccess: vi.fn() }))
vi.mock('../utils/activeProject', () => ({ setActiveProjectId: vi.fn() }))
vi.mock('../hooks/useCountdown', () => ({
  useCountdown: vi.fn(() => ({ remaining: null, isWarning: false, isExpired: false })),
}))

const params = { id: 'run-1' }
vi.mock('react-router-dom', async () => {
  const actual = await vi.importActual('react-router-dom')
  return { ...actual, useParams: () => params }
})

vi.mock('../components/RunMetrics', () => ({ default: () => <div>RunMetricsMock</div> }))
vi.mock('../components/ArtifactsPanel', () => ({
  default: ({ projectId }: { projectId: string }) => <div>{`Artifacts:${projectId || 'none'}`}</div>,
}))
vi.mock('../components/AuditLog', () => ({ default: () => <div>AuditMock</div> }))
vi.mock('./run-detail/RunSensorsPanel', () => ({ default: () => <div>RunSensorsMock</div> }))
vi.mock('./run-detail/RunOverviewGrid', () => ({
  default: (p: {
    actionError: string | null
    formattedStartedAt: string
    formattedFinishedAt: string
    formattedDuration: string
  }) => (
    <div>
      <span data-testid="ov-start">{p.formattedStartedAt}</span>
      <span data-testid="ov-finish">{p.formattedFinishedAt}</span>
      <span data-testid="ov-duration">{p.formattedDuration}</span>
      {p.actionError && <span data-testid="ov-error">{p.actionError}</span>}
    </div>
  ),
}))
vi.mock('../components/TelemetryStreamModal', () => ({
  default: (p: {
    sensorId: string
    isOpen: boolean
    onClose: () => void
    filterCaptureSessionId?: string
  }) => (
    <div data-testid="stream-modal">
      {`stream:${p.sensorId}:${p.isOpen ? 'open' : 'closed'}:${p.filterCaptureSessionId ?? 'none'}`}
      <button onClick={p.onClose}>close-stream</button>
    </div>
  ),
}))
vi.mock('../components/TelemetryExportModal', () => ({
  default: (p: {
    mode: string
    sessionId?: string
    sessionOrdinal?: number
    onClose: () => void
  }) => (
    <div data-testid="export-modal">
      {`export:${p.mode}:${p.sessionId ?? '-'}:${p.sessionOrdinal ?? '-'}`}
      <button onClick={p.onClose}>close-export</button>
    </div>
  ),
}))
vi.mock('./run-detail/RunCaptureSessions', () => ({
  default: (p: {
    createSessionMutation: { mutate: (v: string | undefined) => void }
    deleteSessionMutation: { mutate: (v: string) => void }
    setExportTarget: (
      t: { mode: 'session'; sessionId: string; sessionOrdinal: number } | { mode: 'run' }
    ) => void
    formatSessionDuration: (a?: string | null, b?: string | null) => string
  }) => (
    <div>
      <button onClick={() => p.createSessionMutation.mutate('note-x')}>cs-create</button>
      <button onClick={() => p.deleteSessionMutation.mutate('sess-9')}>cs-delete</button>
      <button onClick={() => p.setExportTarget({ mode: 'run' })}>cs-export-run</button>
      <button
        onClick={() => p.setExportTarget({ mode: 'session', sessionId: 'sess-9', sessionOrdinal: 9 })}
      >
        cs-export-session
      </button>
      <span data-testid="dur-none">{p.formatSessionDuration(null, null)}</span>
      <span data-testid="dur-zero">
        {p.formatSessionDuration('2024-01-01T10:00:00Z', '2024-01-01T10:00:00Z')}
      </span>
      <span data-testid="dur-min">
        {p.formatSessionDuration('2024-01-01T10:00:00Z', '2024-01-01T10:02:05Z')}
      </span>
      <span data-testid="dur-hour">
        {p.formatSessionDuration('2024-01-01T10:00:00Z', '2024-01-01T11:01:01Z')}
      </span>
      <span data-testid="dur-open">{p.formatSessionDuration(new Date().toISOString())}</span>
    </div>
  ),
}))

const experiment = createMockExperiment({ id: 'exp-1', project_id: 'proj-1' })
const sensorA = createMockSensor({ id: 'sa', name: 'Alpha', type: 'temp' })
const sensorB = createMockSensor({ id: 'sb', name: 'Beta', type: 'hum' })

function sessionsResponse(capture_sessions: CaptureSession[]) {
  return { capture_sessions, total: capture_sessions.length, page: 1, page_size: 20 }
}

let queryClient: QueryClient
const wrapper = ({ children }: { children: ReactNode }) => (
  <QueryClientProvider client={queryClient}>
    <MemoryRouter>{children}</MemoryRouter>
  </QueryClientProvider>
)

function setup(run: Partial<Run> = {}) {
  vi.mocked(runsApi.get).mockResolvedValue(
    createMockRun({ id: 'run-1', experiment_id: 'exp-1', name: 'My Run', ...run })
  )
  const user = userEvent.setup()
  render(<RunDetail />, { wrapper })
  return user
}

describe('RunDetail branches', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    queryClient = new QueryClient({
      defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
    })
    vi.stubGlobal('prompt', vi.fn(() => 'notes'))
    vi.stubGlobal('confirm', vi.fn(() => true))
    vi.mocked(experimentsApi.get).mockResolvedValue(experiment)
    vi.mocked(captureSessionsApi.list).mockResolvedValue(sessionsResponse([]))
    vi.mocked(sensorsApi.list).mockResolvedValue({ sensors: [], total: 0, page: 1, page_size: 20 } as never)
    vi.mocked(useCountdown).mockReturnValue({ remaining: null, isWarning: false, isExpired: false })
  })
  afterEach(() => {
    vi.unstubAllGlobals()
  })

  it('shows a draft run start button that is disabled until experiment loads, then starts', async () => {
    let resolveExp: (e: Experiment) => void = () => {}
    vi.mocked(experimentsApi.get).mockReturnValue(
      new Promise<Experiment>((r) => {
        resolveExp = r
      })
    )
    let resolveUpdate: (r: Run) => void = () => {}
    vi.mocked(runsApi.update).mockReturnValue(
      new Promise<Run>((r) => {
        resolveUpdate = r
      })
    )
    const user = setup({
      status: 'draft',
      started_at: null,
      finished_at: '2024-01-02T10:00:00Z',
      duration_seconds: null,
    })
    const start = await screen.findByRole('button', { name: 'Запустить' })
    expect(start).toBeDisabled()
    expect(start).toHaveAttribute('title', 'Загрузка эксперимента (project_id)...')
    expect(screen.getByRole('button', { name: 'Старт отсчёта' })).toBeDisabled()
    expect(screen.getByTestId('ov-start')).toHaveTextContent('—')
    expect(screen.getByTestId('ov-finish')).toHaveTextContent(
      formatDateTimeSeconds('2024-01-02T10:00:00Z')
    )
    expect(screen.getByTestId('ov-duration')).toHaveTextContent('—')
    expect(screen.getByText('Artifacts:none')).toBeInTheDocument()

    // create session from the child while experiment is still missing -> inline error
    await user.click(screen.getByRole('button', { name: 'cs-create' }))
    expect(await screen.findByTestId('ov-error')).toHaveTextContent(
      'Эксперимент ещё не загружен, попробуйте ещё раз'
    )

    await act(async () => {
      resolveExp(experiment)
    })
    await waitFor(() => expect(start).toBeEnabled())
    expect(start).not.toHaveAttribute('title')
    expect(setActiveProjectId).toHaveBeenCalledWith('proj-1')
    await user.click(start)
    expect(await screen.findByRole('button', { name: 'Запуск...' })).toBeDisabled()
    expect(runsApi.update).toHaveBeenCalledWith('run-1', { status: 'running' })
    await act(async () => {
      resolveUpdate(createMockRun({ id: 'run-1', status: 'running' }))
    })
  })

  it('creates a session with the next ordinal and no notes when prompt is cancelled', async () => {
    vi.stubGlobal('prompt', vi.fn(() => null))
    vi.mocked(captureSessionsApi.list).mockResolvedValue(
      sessionsResponse([
        createMockCaptureSession({ id: 'c1', ordinal_number: 2, status: 'succeeded' }),
        createMockCaptureSession({ id: 'c2', ordinal_number: 5, status: 'failed' }),
      ])
    )
    let resolveCreate: (v: CaptureSession) => void = () => {}
    vi.mocked(captureSessionsApi.create).mockReturnValue(
      new Promise<CaptureSession>((r) => {
        resolveCreate = r
      })
    )
    const user = setup({ status: 'running' })
    expect(await screen.findByText(/sessions: 2/)).toHaveTextContent('/ done 2')
    const btn = screen.getByRole('button', { name: 'Старт отсчёта' })
    await waitFor(() => expect(btn).toBeEnabled())
    await user.click(btn)
    expect(await screen.findByRole('button', { name: 'Создание...' })).toBeDisabled()
    expect(captureSessionsApi.create).toHaveBeenCalledWith(
      'run-1',
      { ordinal_number: 6, notes: undefined },
      { project_id: 'proj-1' }
    )
    await act(async () => {
      resolveCreate(createMockCaptureSession({ id: 'c3', ordinal_number: 6 }))
    })
  })

  it('does not stop the session when confirm is declined, shows pending while stopping', async () => {
    vi.mocked(captureSessionsApi.list).mockResolvedValue(
      sessionsResponse([createMockCaptureSession({ id: 'act', ordinal_number: 3, status: 'backfilling' })])
    )
    vi.mocked(sensorsApi.list).mockResolvedValue({ sensors: [sensorA], total: 1 } as never)
    vi.stubGlobal('confirm', vi.fn(() => false))
    let resolveStop: (v: CaptureSession) => void = () => {}
    vi.mocked(captureSessionsApi.stop).mockReturnValue(
      new Promise<CaptureSession>((r) => {
        resolveStop = r
      })
    )
    const user = setup({ status: 'running' })
    const stop = await screen.findByRole('button', { name: 'Остановить отсчёт' })
    expect(screen.getByText(/sessions: 1/)).toHaveTextContent('/ active #3')
    expect(await screen.findByText('capture_session_id: act')).toBeInTheDocument()
    await user.click(stop)
    expect(captureSessionsApi.stop).not.toHaveBeenCalled()
    vi.stubGlobal('confirm', vi.fn(() => true))
    await user.click(stop)
    expect(await screen.findByRole('button', { name: 'Остановка...' })).toBeDisabled()
    expect(captureSessionsApi.stop).toHaveBeenCalledWith('run-1', 'act')
    await act(async () => {
      resolveStop(createMockCaptureSession({ id: 'act', status: 'succeeded' }))
    })
  })

  it('shows the countdown text and warning class', async () => {
    vi.mocked(useCountdown).mockReturnValue({ remaining: 65_000, isWarning: true, isExpired: false })
    setup({ status: 'running', auto_complete_after_minutes: 10, started_at: '2024-01-01T10:00:00Z' })
    const el = await screen.findByText('Автозавершение через 01:05')
    expect(el).toHaveClass('countdown-warning')
    expect(useCountdown).toHaveBeenLastCalledWith(
      new Date('2024-01-01T10:00:00Z').getTime() + 10 * 60 * 1000
    )
  })

  it('shows the expired countdown text', async () => {
    vi.mocked(useCountdown).mockReturnValue({ remaining: 0, isWarning: false, isExpired: true })
    setup({ status: 'running', auto_complete_after_minutes: 1, started_at: '2024-01-01T10:00:00Z' })
    const el = await screen.findByText('Автозавершение ожидается...')
    expect(el).not.toHaveClass('countdown-warning')
  })

  it('has no countdown for running runs without start time', async () => {
    setup({ status: 'running', auto_complete_after_minutes: 1, started_at: null })
    await screen.findByText('My Run')
    expect(useCountdown).toHaveBeenLastCalledWith(null)
    expect(screen.queryByText(/Автозавершение/)).not.toBeInTheDocument()
  })

  it('formats session durations', async () => {
    setup({ status: 'succeeded' })
    await screen.findByText('My Run')
    expect(screen.getByTestId('dur-none')).toHaveTextContent('-')
    expect(screen.getByTestId('dur-zero')).toHaveTextContent('-')
    expect(screen.getByTestId('dur-min')).toHaveTextContent('2м 5с')
    expect(screen.getByTestId('dur-hour')).toHaveTextContent('1ч 1м 1с')
    expect(screen.getByTestId('dur-open')).toHaveTextContent(/^(-|\d+с)$/)
    expect(screen.queryByRole('button', { name: 'Старт отсчёта' })).not.toBeInTheDocument()
  })

  it('opens export modal for run and session targets and closes it', async () => {
    const user = setup({ status: 'succeeded' })
    await screen.findByText('My Run')
    await user.click(screen.getByRole('button', { name: 'cs-export-run' }))
    expect(screen.getByTestId('export-modal')).toHaveTextContent('export:run:-:-')
    await user.click(screen.getByRole('button', { name: 'cs-export-session' }))
    expect(screen.getByTestId('export-modal')).toHaveTextContent('export:session:sess-9:9')
    await user.click(screen.getByRole('button', { name: 'close-export' }))
    expect(screen.queryByTestId('export-modal')).not.toBeInTheDocument()
  })

  it('deletes a session through the child component', async () => {
    vi.mocked(captureSessionsApi.delete).mockResolvedValue(undefined as never)
    const user = setup({ status: 'succeeded' })
    await screen.findByText('My Run')
    await user.click(screen.getByRole('button', { name: 'cs-delete' }))
    await waitFor(() => expect(captureSessionsApi.delete).toHaveBeenCalledWith('run-1', 'sess-9'))
  })

  it('shows sensors loading, then selects sensors and opens the live stream', async () => {
    let resolveSensors: (v: unknown) => void = () => {}
    vi.mocked(sensorsApi.list).mockReturnValue(
      new Promise((r) => {
        resolveSensors = r
      }) as never
    )
    const user = setup({ status: 'succeeded' })
    expect(await screen.findByText('Загрузка датчиков...')).toBeInTheDocument()
    await act(async () => {
      resolveSensors({ sensors: [sensorA, sensorB], total: 2 })
    })
    expect(await screen.findByText('stream:sa:closed:none')).toBeInTheDocument()
    expect(screen.getByText('sensors: 2')).toBeInTheDocument()
    await pickMaterialSelectOption(user, 'Датчик', /Beta/)
    expect(screen.getByText('stream:sb:closed:none')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'Live telemetry' }))
    expect(screen.getByText('stream:sb:open:none')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'close-stream' }))
    expect(screen.getByText('stream:sb:closed:none')).toBeInTheDocument()

    // sensor list changes and no longer contains the selected sensor -> falls back to first
    await act(async () => {
      queryClient.setQueryData(['sensors', 'proj-1'], { sensors: [sensorA], total: 1 })
    })
    expect(await screen.findByText('stream:sa:closed:none')).toBeInTheDocument()
  })

  it('shows empty sensors state when project has none', async () => {
    setup({ status: 'succeeded' })
    expect(await screen.findByText('В проекте нет датчиков')).toBeInTheDocument()
    expect(screen.queryByTestId('stream-modal')).not.toBeInTheDocument()
  })
})
