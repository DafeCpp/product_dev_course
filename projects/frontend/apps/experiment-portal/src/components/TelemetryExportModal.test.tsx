import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import TelemetryExportModal from './TelemetryExportModal'
import type { CaptureSession, Sensor } from '../types'

vi.mock('../api/client', () => ({
  telemetryExportApi: { exportSession: vi.fn(), exportRun: vi.fn() },
}))
import { telemetryExportApi } from '../api/client'

const sensors = [{ id: 'sn1', name: 'Temp', type: 'temperature' }] as unknown as Sensor[]
const sessions = [
  { id: 'cs1', ordinal_number: 1, status: 'succeeded', started_at: '2026-01-01T10:00:00Z' },
  { id: 'cs2', ordinal_number: 2, status: 'running', started_at: null },
] as unknown as CaptureSession[]

const downloads: string[] = []

beforeEach(() => {
  vi.clearAllMocks()
  downloads.length = 0
  Object.assign(URL, { createObjectURL: vi.fn(() => 'blob:x'), revokeObjectURL: vi.fn() })
  vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(function (this: HTMLAnchorElement) { downloads.push(this.download) })
})

const selects = () => screen.getAllByRole('combobox')

describe('TelemetryExportModal', () => {
  it('exports the whole run as CSV with filters', async () => {
    vi.mocked(telemetryExportApi.exportRun).mockResolvedValue('a,b')
    const onClose = vi.fn()
    render(<TelemetryExportModal isOpen onClose={onClose} runId="r1" mode="run" sessions={sessions} sensors={sensors} />)
    expect(screen.getByText('Экспорт телеметрии — весь запуск')).toBeInTheDocument()
    const [values, sensor, session] = selects()
    await userEvent.selectOptions(values, 'raw')
    await userEvent.selectOptions(sensor, 'sn1')
    await userEvent.selectOptions(session, 'cs2')
    await userEvent.type(screen.getByPlaceholderText(/temperature/), '  temp ')
    await userEvent.click(screen.getByLabelText(/Включить опоздавшие/))
    await userEvent.click(screen.getByText('Скачать CSV'))
    await waitFor(() => expect(onClose).toHaveBeenCalled())
    expect(telemetryExportApi.exportRun).toHaveBeenCalledWith('r1', {
      format: 'csv', raw_or_physical: 'raw', include_late: false, aggregation: undefined,
      sensor_id: 'sn1', signal: 'temp', capture_session_id: 'cs2',
    })
    expect(downloads).toEqual(['telemetry_run_r1.csv'])
  })

  it('exports one session as aggregated JSON and ignores the values select', async () => {
    vi.mocked(telemetryExportApi.exportSession).mockResolvedValue('{}')
    const onClose = vi.fn()
    render(<TelemetryExportModal isOpen onClose={onClose} runId="r1" mode="session" sessionId="cs1" sessionOrdinal={4} sensors={sensors} />)
    expect(screen.getByText('Экспорт телеметрии — сессия #4')).toBeInTheDocument()
    expect(screen.queryByText('Сессия (фильтр)')).toBeNull()
    await userEvent.click(screen.getByLabelText('JSON'))
    await userEvent.click(screen.getByLabelText(/Агрегация 1m/))
    expect(selects()[0]).toBeDisabled()
    await userEvent.click(screen.getByText('Скачать JSON'))
    await waitFor(() => expect(onClose).toHaveBeenCalled())
    expect(telemetryExportApi.exportSession).toHaveBeenCalledWith('r1', 'cs1', expect.objectContaining({
      format: 'json', raw_or_physical: undefined, aggregation: '1m', include_late: true,
    }))
    expect(downloads).toEqual(['telemetry_session_4.json'])
  })

  it('names the file by session id when there is no ordinal, using prefilled values', async () => {
    vi.mocked(telemetryExportApi.exportSession).mockResolvedValue('x')
    render(<TelemetryExportModal isOpen onClose={vi.fn()} runId="r1" mode="session" sessionId="cs9" sensors={sensors}
      initialRawOrPhysical="physical" initialIncludeLate={false} initialSensorId="sn1" />)
    expect(selects()[0]).toHaveValue('physical')
    await userEvent.click(screen.getByText('Скачать CSV'))
    await waitFor(() => expect(downloads).toEqual(['telemetry_session_cs9.csv']))
  })

  it('prefills the session filter in run mode', () => {
    render(<TelemetryExportModal isOpen onClose={vi.fn()} runId="r1" mode="run" sessions={sessions} sensors={sensors} initialCaptureSessionId="cs1" />)
    expect(selects()[2]).toHaveValue('cs1')
  })

  it('reports an error, stays open and re-enables the form', async () => {
    vi.mocked(telemetryExportApi.exportRun).mockRejectedValue(new Error('boom'))
    const onClose = vi.fn()
    render(<TelemetryExportModal isOpen onClose={onClose} runId="r1" mode="run" sensors={sensors} />)
    await userEvent.click(screen.getByText('Скачать CSV'))
    await waitFor(() => expect(telemetryExportApi.exportRun).toHaveBeenCalled())
    await waitFor(() => expect(screen.getByText('Скачать CSV')).toBeEnabled())
    expect(onClose).not.toHaveBeenCalled()
    expect(downloads).toEqual([])
    await userEvent.click(screen.getByText('Отмена'))
    expect(onClose).toHaveBeenCalled()
  })

  it('renders nothing when closed', () => {
    render(<TelemetryExportModal isOpen={false} onClose={vi.fn()} runId="r1" mode="run" sensors={sensors} />)
    expect(screen.queryByText('Формат файла')).toBeNull()
  })
})
