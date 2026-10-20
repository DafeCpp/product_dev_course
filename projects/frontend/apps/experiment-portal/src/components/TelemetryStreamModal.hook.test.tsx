import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import TelemetryStreamModal from './TelemetryStreamModal'
import { telemetryApi } from '../api/client'
import { maybeEmitHttpErrorToast } from '../utils/httpDebug'
import { pickMaterialSelectOption } from '../testUtils/materialSelect'

vi.mock('../api/client', () => ({ telemetryApi: { stream: vi.fn() } }))
vi.mock('../utils/httpDebug', async (orig) => ({
  ...(await orig<typeof import('../utils/httpDebug')>()),
  maybeEmitHttpErrorToast: vi.fn(),
}))

const stream = {
  status: 'idle' as string,
  points: [] as any[],
  error: null as Error | null,
  cursor: {} as { sinceId?: number; sinceTs?: string },
  start: vi.fn(),
  stop: vi.fn(),
  clear: vi.fn(),
}
const hookArgs: { current: any } = { current: null }
vi.mock('frontend-common', () => ({
  useTelemetryStream: (_id: string, opts: unknown) => {
    hookArgs.current = opts
    return stream
  },
}))

function rec(id: number, over: Record<string, unknown> = {}) {
  return {
    id, sensor_id: 's1', project_id: 'p1', timestamp: '2026-01-01T00:00:00Z',
    raw_value: id, physical_value: id * 10, run_id: null, capture_session_id: null, meta: {}, ...over,
  }
}

function renderModal(props: Partial<React.ComponentProps<typeof TelemetryStreamModal>> = {}) {
  const onClose = vi.fn()
  const utils = render(<TelemetryStreamModal sensorId="s1" isOpen onClose={onClose} {...props} />)
  return { onClose, ...utils }
}

const debug = { url: 'http://x/stream', headers: { A: 'b' }, method: 'GET' }

describe('TelemetryStreamModal (stream states)', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    Object.assign(stream, { status: 'idle', points: [], error: null, cursor: {} })
  })

  it('starts the stream with parsed cursor inputs', async () => {
    const user = userEvent.setup()
    renderModal()
    const ts = screen.getByLabelText('since_ts')
    await user.clear(ts)
    await user.type(ts, '2026-02-02T00:00:00Z')
    await user.clear(screen.getByLabelText(/since_id/))
    await user.type(screen.getByLabelText(/since_id/), '42')
    await user.click(screen.getByRole('button', { name: 'Старт' }))
    expect(stream.start).toHaveBeenCalledWith({ sinceTs: '2026-02-02T00:00:00Z', sinceId: 42 })
  })

  it('falls back to now for an invalid timestamp and to 0 for empty since_id', async () => {
    const user = userEvent.setup()
    renderModal()
    await user.clear(screen.getByLabelText('since_ts'))
    await user.type(screen.getByLabelText('since_ts'), 'garbage')
    await user.clear(screen.getByLabelText(/since_id/))
    await user.click(screen.getByRole('button', { name: 'Старт' }))
    const arg = stream.start.mock.calls[0][0]
    expect(Number.isFinite(Date.parse(arg.sinceTs))).toBe(true)
    expect(arg.sinceTs).not.toBe('garbage')
    expect(arg.sinceId).toBe(0)
  })

  it('clamps buffer size and idle timeout passed to the stream', async () => {
    const user = userEvent.setup()
    renderModal()
    await user.clear(screen.getByLabelText(/max points/))
    await user.type(screen.getByLabelText(/max points/), '99999')
    await user.clear(screen.getByLabelText('idle_timeout_seconds'))
    await user.type(screen.getByLabelText('idle_timeout_seconds'), '0')
    expect(hookArgs.current.bufferSize).toBe(2000)
    expect(hookArgs.current.idleTimeoutSeconds).toBe(1)
    await user.clear(screen.getByLabelText(/max points/))
    await user.clear(screen.getByLabelText('idle_timeout_seconds'))
    expect(hookArgs.current.bufferSize).toBe(200)
    expect(hookArgs.current.idleTimeoutSeconds).toBe(30)
  })

  it('shows a connecting button disabled and blocks closing', async () => {
    stream.status = 'connecting'
    renderModal()
    const btn = screen.getByRole('button', { name: 'Подключение…' })
    expect(btn).toBeDisabled()
    expect(screen.getByLabelText('since_ts')).toBeDisabled()
  })

  it('shows a Stop button while streaming and stops on click', async () => {
    const user = userEvent.setup()
    stream.status = 'streaming'
    renderModal()
    await user.click(screen.getByRole('button', { name: 'Стоп' }))
    expect(stream.stop).toHaveBeenCalled()
  })

  it('shows a reconnecting hint with the cursor', () => {
    Object.assign(stream, { status: 'reconnecting', cursor: { sinceId: 7, sinceTs: '2026-01-01T00:00:07Z' } })
    renderModal()
    expect(screen.getByText('переподключение…')).toBeInTheDocument()
    expect(screen.getByText('last_id: 7')).toBeInTheDocument()
    expect(screen.getByText('last_ts: 2026-01-01T00:00:07Z')).toBeInTheDocument()
  })

  it('shows the stream error', () => {
    Object.assign(stream, { status: 'error', error: new Error('stream down') })
    renderModal()
    expect(screen.getByRole('alert')).toBeInTheDocument()
  })

  it('stops and closes via the modal close button when allowed', async () => {
    const user = userEvent.setup()
    const { onClose } = renderModal()
    await user.keyboard('{Escape}')
    await waitFor(() => expect(onClose).toHaveBeenCalled())
    expect(stream.stop).toHaveBeenCalled()
  })

  it('does not close while connecting', async () => {
    const user = userEvent.setup()
    stream.status = 'connecting'
    const { onClose } = renderModal()
    await user.keyboard('{Escape}')
    expect(onClose).not.toHaveBeenCalled()
  })

  it('stops and clears when the modal closes', () => {
    const { rerender, onClose } = renderModal()
    expect(stream.clear).not.toHaveBeenCalled()
    rerender(<TelemetryStreamModal sensorId="s1" isOpen={false} onClose={onClose} />)
    expect(stream.stop).toHaveBeenCalled()
    expect(stream.clear).toHaveBeenCalled()
  })

  describe('records and sparkline', () => {
    it('renders latest events newest first with optional fields', () => {
      stream.points = [rec(1), rec(2, { physical_value: null, run_id: 'r1', capture_session_id: 'c1' })]
      renderModal()
      expect(screen.getByText('events: 2')).toBeInTheDocument()
      expect(screen.getByText('run=r1')).toBeInTheDocument()
      expect(screen.getByText('cs=c1')).toBeInTheDocument()
      expect(screen.getByText('raw=1 phys=10')).toBeInTheDocument()
      expect(screen.queryByText('Событий пока нет')).not.toBeInTheDocument()
    })

    it('shows empty states without data', () => {
      renderModal()
      expect(screen.getByText('Событий пока нет')).toBeInTheDocument()
      expect(screen.getByText('нет данных')).toBeInTheDocument()
      expect(screen.getByText(/Нет данных — нажмите/)).toBeInTheDocument()
    })

    it.each([
      [[5000, 6000], 'last=6000'],
      [[150, 250], 'last=250.0'],
      [[15, 25], 'last=25.00'],
      [[1, 2], 'last=2.000'],
    ])('formats %j values like %s', (values, expected) => {
      stream.points = values.map((v, i) => rec(i + 1, { physical_value: v }))
      renderModal()
      expect(screen.getByText((t) => t.startsWith(expected))).toBeInTheDocument()
    })

    it('draws a flat series and a single point without NaN paths', () => {
      stream.points = [rec(1, { physical_value: 5 })]
      const { rerender, onClose } = renderModal()
      expect(document.querySelector('.sparkline-path')!.getAttribute('d')).not.toContain('NaN')
      expect(document.querySelector('.sparkline-last')).toBeInTheDocument()
      stream.points = [rec(1, { physical_value: 0 }), rec(2, { physical_value: 0 })]
      rerender(<TelemetryStreamModal sensorId="s1" isOpen onClose={onClose} />)
      expect(document.querySelector('.sparkline-path')!.getAttribute('d')).not.toContain('NaN')
    })

    it('switches between physical and raw values, ignoring non-numeric values', async () => {
      const user = userEvent.setup()
      stream.points = [rec(1, { raw_value: 2, physical_value: 20 }), rec(2, { raw_value: 'x', physical_value: 'y' })]
      renderModal()
      expect(screen.getByText((t) => t.startsWith('last=20.00'))).toBeInTheDocument()
      await user.click(screen.getByLabelText('raw'))
      expect(screen.getByText((t) => t.startsWith('last=2.000'))).toBeInTheDocument()
      await user.click(screen.getByLabelText('physical'))
      expect(screen.getByText((t) => t.startsWith('last=20.00'))).toBeInTheDocument()
    })

    it('limits the plotted series to the last 50 values and the list to 25 rows', () => {
      stream.points = Array.from({ length: 60 }, (_, i) => rec(i + 1, { physical_value: i }))
      renderModal()
      expect(document.querySelectorAll('.record-row')).toHaveLength(25)
      expect(screen.getByText('#60')).toBeInTheDocument()
      expect(screen.getByText((t) => t.startsWith('last=59.00'))).toBeInTheDocument()
    })
  })

  describe('filters', () => {
    const points = () => [
      rec(1, { run_id: 'r1', capture_session_id: 'c1' }),
      rec(2, { run_id: 'r1', capture_session_id: 'c2' }),
      rec(3, { run_id: 'r2', capture_session_id: null }),
    ]

    it('has no filter control without run or session', () => {
      renderModal()
      expect(screen.queryByLabelText('filter')).not.toBeInTheDocument()
    })

    it('defaults to the run filter when only a run is given', () => {
      stream.points = points()
      renderModal({ filterRunId: 'r1' })
      expect(screen.getByText('events: 2 / 3')).toBeInTheDocument()
      expect(screen.getByText('run_id: r1')).toBeInTheDocument()
    })

    it('defaults to the capture filter when a session is given and lets the user switch', async () => {
      const user = userEvent.setup()
      stream.points = points()
      renderModal({ filterRunId: 'r1', filterCaptureSessionId: 'c1' })
      expect(screen.getByText('events: 1 / 3')).toBeInTheDocument()
      expect(screen.getByText('cs_id: c1')).toBeInTheDocument()
      await pickMaterialSelectOption(user, 'filter', 'this run')
      expect(screen.getByText('events: 2 / 3')).toBeInTheDocument()
      await pickMaterialSelectOption(user, 'filter', 'all events')
      expect(screen.getByText('events: 3')).toBeInTheDocument()
    })
  })

  describe('opener', () => {
    const ctx = { sinceTs: 't', sinceId: 1, idleTimeoutSeconds: 5, signal: new AbortController().signal }

    it('returns the response of a healthy stream', async () => {
      const response = { ok: true, status: 200 }
      vi.mocked(telemetryApi.stream).mockResolvedValue({ response, debug } as never)
      renderModal()
      await expect(hookArgs.current.open(ctx)).resolves.toEqual({ response })
      expect(telemetryApi.stream).toHaveBeenCalledWith({
        sensor_id: 's1', since_ts: 't', since_id: 1, idle_timeout_seconds: 5, signal: ctx.signal,
      })
    })

    it('emits an http error toast and throws for a failed response', async () => {
      const resp = { ok: false, status: 503, statusText: 'Down', headers: new Headers({ 'x-a': 'b' }), text: vi.fn().mockResolvedValue('overloaded') }
      vi.mocked(telemetryApi.stream).mockResolvedValue({ response: resp, debug } as never)
      renderModal()
      await expect(hookArgs.current.open(ctx)).rejects.toBeInstanceOf(Error)
      expect(maybeEmitHttpErrorToast).toHaveBeenCalledTimes(1)
    })

    it('handles a failed response whose body cannot be read', async () => {
      const resp = { ok: false, status: 500, statusText: 'Err', headers: new Headers(), text: vi.fn().mockRejectedValue(new Error('x')) }
      vi.mocked(telemetryApi.stream).mockResolvedValue({ response: resp, debug } as never)
      renderModal()
      await expect(hookArgs.current.open(ctx)).rejects.toBeTruthy()
      expect(maybeEmitHttpErrorToast).toHaveBeenCalled()
    })

    it('rethrows aborts silently', async () => {
      const abort = Object.assign(new Error('aborted'), { name: 'AbortError' })
      vi.mocked(telemetryApi.stream).mockRejectedValue(abort)
      renderModal()
      await expect(hookArgs.current.open(ctx)).rejects.toBe(abort)
      expect(maybeEmitHttpErrorToast).not.toHaveBeenCalled()
    })

    it('toasts connection errors carrying debug info and rethrows', async () => {
      const err = Object.assign(new Error('refused'), { debug })
      vi.mocked(telemetryApi.stream).mockRejectedValue(err)
      renderModal()
      await expect(hookArgs.current.open(ctx)).rejects.toBe(err)
      expect(maybeEmitHttpErrorToast).toHaveBeenCalledTimes(1)
    })

    it('uses a default message for connection errors without a message', async () => {
      const err = Object.assign(new Error(''), { debug })
      vi.mocked(telemetryApi.stream).mockRejectedValue(err)
      renderModal()
      await expect(hookArgs.current.open(ctx)).rejects.toBe(err)
      expect(maybeEmitHttpErrorToast).toHaveBeenCalledTimes(1)
    })

    it('does not toast plain errors without debug info', async () => {
      vi.mocked(telemetryApi.stream).mockRejectedValue(new Error('plain'))
      renderModal()
      await expect(hookArgs.current.open(ctx)).rejects.toThrow('plain')
      expect(maybeEmitHttpErrorToast).not.toHaveBeenCalled()
    })
  })
})
