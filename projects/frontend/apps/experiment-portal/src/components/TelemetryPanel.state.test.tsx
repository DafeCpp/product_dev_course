import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor, act, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import Plotly from 'plotly.js-dist-min'
import TelemetryPanel from './TelemetryPanel'
import { telemetryApi } from '../api/client'
import type { Sensor } from '../types'

vi.mock('../api/client', () => ({ telemetryApi: { stream: vi.fn() } }))
vi.mock('plotly.js-dist-min', () => ({
  default: { react: vi.fn(), purge: vi.fn(), relayout: vi.fn(), Plots: { resize: vi.fn() } },
}))

type FakeStream = { status: string; points: any[]; lastRecord: any }
const streams: Record<string, FakeStream> = {}
const opts: Record<string, any> = {}
const startSpies: Record<string, ReturnType<typeof vi.fn>> = {}

vi.mock('frontend-common', () => ({
  useTelemetryStream: (sensorId: string, o: any) => {
    opts[sensorId] = o
    const s = streams[sensorId] ?? (streams[sensorId] = { status: 'idle', points: [], lastRecord: null })
    const start = startSpies[sensorId] ?? (startSpies[sensorId] = vi.fn())
    return { ...s, start, stop: vi.fn(), clear: vi.fn() }
  },
}))

function sensor(id: string, name = `Sensor ${id}`): Sensor {
  return {
    id, project_id: 'p1', name, type: 'temperature', input_unit: 'raw', display_unit: 'C', status: 'active',
    connection_status: 'online', token_preview: null, last_heartbeat: null, active_profile_id: null,
    calibration_notes: null, created_at: '2026-01-01T00:00:00Z', updated_at: '2026-01-01T00:00:00Z',
  } as Sensor
}

const sensors = [sensor('s1'), sensor('s2')]
const KEY = 'telemetry_panel_state_pan'

function rec(id: number, ts: string, phys: number | null, raw: number | null = phys) {
  return { id, sensor_id: 's1', project_id: 'p1', timestamp: ts, physical_value: phys, raw_value: raw, run_id: null, capture_session_id: null, meta: {} }
}

function renderPanel(props: Partial<React.ComponentProps<typeof TelemetryPanel>> = {}) {
  const onRemove = vi.fn()
  const utils = render(<TelemetryPanel panelId="pan" title="Panel" sensors={sensors} onRemove={onRemove} {...props} />)
  return { onRemove, ...utils }
}

async function openSettings(user: ReturnType<typeof userEvent.setup>) {
  await user.click(screen.getByRole('button', { name: /настройки/i }))
  return screen.getByRole('dialog', { name: 'Настройки панели' })
}

async function addSensor(user: ReturnType<typeof userEvent.setup>, name: RegExp) {
  await user.click(screen.getByRole('button', { name: /добавить сенсор/i }))
  await user.click(await screen.findByRole('option', { name }))
}

describe('TelemetryPanel (state and rendering)', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    window.localStorage.clear()
    for (const k of Object.keys(streams)) delete streams[k]
    for (const k of Object.keys(startSpies)) delete startSpies[k]
    vi.spyOn(console, 'debug').mockImplementation(() => {})
  })
  afterEach(() => {
    vi.unstubAllGlobals()
    vi.restoreAllMocks()
  })

  describe('restoring saved state', () => {
    it('restores title, sensors, mode, window and anchor', async () => {
      window.localStorage.setItem(KEY, JSON.stringify({
        title: 'Saved', selectedSensorIds: ['s2', 'ghost', 5], valueMode: 'raw', timeWindowSeconds: 60, useLatestAnchor: false,
      }))
      renderPanel()
      expect(screen.getByPlaceholderText('Название панели')).toHaveValue('Saved')
      expect(screen.getByLabelText('raw')).toBeChecked()
      expect(screen.getByLabelText('1м')).toBeChecked()
      expect(screen.getByLabelText('последние')).not.toBeChecked()
      // the unknown id is dropped once sensors are known
      expect(await screen.findByText('Sensor s2')).toBeInTheDocument()
    })

    it('supports the legacy minutes-based window', () => {
      window.localStorage.setItem(KEY, JSON.stringify({ timeWindowMinutes: 2 }))
      renderPanel()
      expect(screen.getByLabelText('2м')).toBeChecked()
    })

    it('ignores an unsupported legacy window and invalid scalar values', () => {
      window.localStorage.setItem(KEY, JSON.stringify({
        title: 7, timeWindowMinutes: 7, valueMode: 'weird', timeWindowSeconds: 42, useLatestAnchor: 'yes', selectedSensorIds: 'nope',
      }))
      renderPanel()
      expect(screen.getByPlaceholderText('Название панели')).toHaveValue('Panel')
      expect(screen.getByLabelText('5м')).toBeChecked()
      expect(screen.getByLabelText('physical')).toBeChecked()
      expect(screen.getByLabelText('последние')).toBeChecked()
    })

    it('ignores malformed json', () => {
      window.localStorage.setItem(KEY, '{nope')
      expect(() => renderPanel()).not.toThrow()
    })

    it('uses restored per-sensor cursors and the start timestamp on the first start', async () => {
      const user = userEvent.setup()
      window.localStorage.setItem(KEY, JSON.stringify({
        selectedSensorIds: ['s1', 's2'],
        startFromTimestamp: '2026-01-01T00:00:00Z',
        startFromCursorBySensor: {
          s1: { timestamp: '2026-01-02T00:00:00Z', id: 5.9 },
          bad1: null, bad2: 'x', bad3: { timestamp: 3, id: 1 }, bad4: { timestamp: 't', id: 'x' }, bad5: { timestamp: 't', id: Infinity },
        },
      }))
      renderPanel()
      await user.click(await screen.findByRole('button', { name: /старт/i }))
      expect(opts.s1.initialSinceTs).toBe('2026-01-02T00:00:00Z')
      expect(opts.s1.initialSinceId).toBe(5)
      expect(opts.s2.initialSinceTs).toBe('2026-01-01T00:00:00Z')
      expect(opts.s2.initialSinceId).toBe(0)
    })

    it('ignores cursors that are all invalid', async () => {
      const user = userEvent.setup()
      window.localStorage.setItem(KEY, JSON.stringify({ selectedSensorIds: ['s1'], startFromCursorBySensor: { s1: { id: 1 } } }))
      renderPanel()
      await user.click(await screen.findByRole('button', { name: /старт/i }))
      expect(opts.s1.initialSinceId).toBe(0)
      expect(Number.isFinite(Date.parse(opts.s1.initialSinceTs))).toBe(true)
    })

    it('restores a saved size with minimum bounds', () => {
      const onSizeChange = vi.fn()
      window.localStorage.setItem(KEY, JSON.stringify({ size: { width: 100, height: 100.4 } }))
      const { container } = renderPanel({ onSizeChange })
      expect(onSizeChange).toHaveBeenCalledWith({ width: 280, height: 240 })
      expect((container.firstChild as HTMLElement).style.width).toBe('280px')
    })
  })

  describe('persistence', () => {
    it('saves changes after the initial render', async () => {
      const user = userEvent.setup()
      renderPanel()
      await user.click(screen.getByLabelText('raw'))
      await waitFor(() => expect(JSON.parse(window.localStorage.getItem(KEY)!)).toMatchObject({ valueMode: 'raw' }))
      await user.click(screen.getByLabelText('10с'))
      await user.click(screen.getByLabelText('последние'))
      const saved = JSON.parse(window.localStorage.getItem(KEY)!)
      expect(saved).toMatchObject({ timeWindowSeconds: 10, useLatestAnchor: false })
    })

    it('renames the panel and falls back to the prop title when emptied', async () => {
      const user = userEvent.setup()
      renderPanel()
      const input = screen.getByPlaceholderText('Название панели')
      await user.clear(input)
      await waitFor(() => expect(input).toHaveValue('Panel'))
      await user.type(input, '!')
      expect(input).toHaveValue('Panel!')
    })
  })

  describe('settings popover', () => {
    it('toggles and closes on outside click but not on inside click', async () => {
      const user = userEvent.setup()
      renderPanel()
      const dialog = await openSettings(user)
      await user.click(within(dialog).getByText('Сенсоры'))
      expect(screen.getByRole('dialog', { name: 'Настройки панели' })).toBeInTheDocument()
      await user.click(document.body)
      expect(screen.queryByRole('dialog', { name: 'Настройки панели' })).not.toBeInTheDocument()
      await openSettings(user)
      await user.click(screen.getByRole('button', { name: /настройки/i }))
      expect(screen.queryByRole('dialog', { name: 'Настройки панели' })).not.toBeInTheDocument()
    })

    it('explains loading, error and empty sensor lists', async () => {
      const user = userEvent.setup()
      const { rerender, onRemove } = renderPanel({ sensorsLoading: true })
      await openSettings(user)
      expect(screen.getByText('Загрузка списка сенсоров…')).toBeInTheDocument()
      rerender(<TelemetryPanel panelId="pan" title="Panel" sensors={[]} sensorsError="boom" onRemove={onRemove} />)
      expect(screen.getByText('boom')).toBeInTheDocument()
      rerender(<TelemetryPanel panelId="pan" title="Panel" sensors={[]} onRemove={onRemove} />)
      expect(screen.getByText(/Выберите проект с сенсорами/)).toBeInTheDocument()
      expect(screen.getByText('Сенсоры не выбраны')).toBeInTheDocument()
    })

    it('adds and removes sensors, persisting immediately', async () => {
      const user = userEvent.setup()
      renderPanel()
      await openSettings(user)
      await addSensor(user, /sensor s1/i)
      await waitFor(() => expect(JSON.parse(window.localStorage.getItem(KEY)!).selectedSensorIds).toEqual(['s1']))
      // picking an option from the portaled list counts as an outside click and closes the popover
      await openSettings(user).catch(() => null)
      expect((await screen.findAllByText('Sensor s1')).length).toBeGreaterThan(0)
      await user.click(screen.getByRole('button', { name: 'Удалить сенсор' }))
      expect(screen.getByText('Сенсоры не выбраны')).toBeInTheDocument()
      expect(JSON.parse(window.localStorage.getItem(KEY)!).selectedSensorIds).toEqual([])
    })

    it('does not offer selected sensors again and disables the picker when none remain', async () => {
      const user = userEvent.setup()
      window.localStorage.setItem(KEY, JSON.stringify({ selectedSensorIds: ['s1', 's2'] }))
      renderPanel()
      await openSettings(user)
      expect(screen.getByRole('button', { name: /добавить сенсор/i })).toBeDisabled()
    })
  })

  describe('streaming', () => {
    function seedSensors(ids = ['s1']) {
      window.localStorage.setItem(KEY, JSON.stringify({ selectedSensorIds: ids }))
    }

    it('disables Start without sensors', () => {
      renderPanel()
      expect(screen.getByRole('button', { name: /старт/i })).toBeDisabled()
    })

    it('shows connecting, then streaming with Stop, then stops everything', async () => {
      const user = userEvent.setup()
      seedSensors()
      renderPanel()
      await user.click(await screen.findByRole('button', { name: /старт/i }))
      expect(screen.getByRole('button', { name: /подключение/i })).toBeDisabled()
      streams.s1 = { status: 'streaming', points: [], lastRecord: null }
      await act(async () => { await Promise.resolve() })
      // trigger re-render of the stream child
      await user.click(screen.getByLabelText('raw'))
      await user.click(await screen.findByRole('button', { name: /стоп/i }))
      expect(await screen.findByRole('button', { name: /старт/i })).toBeInTheDocument()
    })

    it('treats reconnecting as streaming', async () => {
      const user = userEvent.setup()
      seedSensors()
      streams.s1 = { status: 'reconnecting', points: [], lastRecord: null }
      renderPanel()
      await user.click(await screen.findByRole('button', { name: /старт/i }))
      expect(await screen.findByRole('button', { name: /стоп/i })).toBeInTheDocument()
    })

    it('reports a final stream error', async () => {
      const user = userEvent.setup()
      seedSensors()
      streams.s1 = { status: 'error', points: [], lastRecord: null }
      renderPanel()
      await user.click(await screen.findByRole('button', { name: /старт/i }))
      await waitFor(() => expect(opts.s1).toBeTruthy())
      act(() => opts.s1.onError(new Error('x'), { willRetry: false }))
      expect(await screen.findByRole('alert')).toBeInTheDocument()
      // errors that will be retried are not surfaced
    })

    it('ignores errors that will be retried', async () => {
      const user = userEvent.setup()
      seedSensors()
      renderPanel()
      await user.click(await screen.findByRole('button', { name: /старт/i }))
      act(() => opts.s1.onError(new Error('x'), { willRetry: true }))
      expect(screen.queryByRole('alert')).not.toBeInTheDocument()
    })

    it('stays in "connecting" if one sensor errors while another connects', async () => {
      const user = userEvent.setup()
      seedSensors(['s1', 's2'])
      streams.s1 = { status: 'error', points: [], lastRecord: null }
      streams.s2 = { status: 'connecting', points: [], lastRecord: null }
      renderPanel()
      await user.click(await screen.findByRole('button', { name: /старт/i }))
      expect(await screen.findByRole('button', { name: /подключение/i })).toBeInTheDocument()
    })

    it('opens streams with the api and rejects bad responses', async () => {
      const user = userEvent.setup()
      seedSensors()
      renderPanel()
      await user.click(await screen.findByRole('button', { name: /старт/i }))
      const ctl = new AbortController()
      vi.mocked(telemetryApi.stream).mockResolvedValueOnce({ response: { ok: true, body: {} } } as never)
      await expect(opts.s1.open({ sinceTs: 't', sinceId: 1, idleTimeoutSeconds: 3, signal: ctl.signal })).resolves.toBeTruthy()
      vi.mocked(telemetryApi.stream).mockResolvedValueOnce({ response: { ok: true, body: null } } as never)
      await expect(opts.s1.open({ signal: ctl.signal })).rejects.toThrow('Stream body is empty')
      vi.mocked(telemetryApi.stream).mockResolvedValueOnce({
        response: { ok: false, status: 500, text: () => Promise.resolve('bad') },
      } as never)
      await expect(opts.s1.open({ signal: ctl.signal })).rejects.toBeTruthy()
    })
  })

  describe('chart data', () => {
    const t = (s: number) => new Date(Date.UTC(2026, 0, 1, 0, 0, s)).toISOString()

    async function startWith(points: any[], extra: Record<string, unknown> = {}) {
      const user = userEvent.setup()
      window.localStorage.setItem(KEY, JSON.stringify({ selectedSensorIds: ['s1'], ...extra }))
      streams.s1 = { status: 'streaming', points, lastRecord: points[points.length - 1] ?? null }
      const onRecordReceived = vi.fn()
      renderPanel({ onRecordReceived })
      await user.click(await screen.findByRole('button', { name: /старт/i }))
      return { user, onRecordReceived }
    }

    it('plots numeric points inside the window anchored on the latest record', async () => {
      await startWith([rec(1, t(0), 1), rec(2, t(500), 2), rec(3, t(520), 3), rec(4, 'bad', 4), rec(5, t(521), null)], { timeWindowSeconds: 60 })
      await waitFor(() => expect(screen.getByText('last=3.000')).toBeInTheDocument())
      const lastCall = vi.mocked(Plotly.react).mock.calls.at(-1) as unknown as [unknown, Array<{ y: number[] }>]
      expect(lastCall[1][0].y).toEqual([2, 3])
    })

    it('anchors the window on the current time when the latest anchor is off', async () => {
      await startWith([rec(1, t(0), 1)], { useLatestAnchor: false, timeWindowSeconds: 5 })
      await waitFor(() => expect(Plotly.react).toHaveBeenCalled())
      expect(screen.getByText('Нет данных — нажмите «Старт»')).toBeInTheDocument()
    })

    it('shows raw values when raw mode is selected', async () => {
      await startWith([rec(1, t(0), 1, 11)], { valueMode: 'raw' })
      expect(await screen.findByText('last=11.000')).toBeInTheDocument()
    })

    it('reports each received record value for the selected mode', async () => {
      const { onRecordReceived } = await startWith([rec(1, t(0), 1, 11)], { valueMode: 'raw' })
      await waitFor(() => expect(onRecordReceived).toHaveBeenCalledWith('s1', 11))
    })

    it('does not report non-numeric record values', async () => {
      const { onRecordReceived } = await startWith([rec(1, t(0), null, null)])
      await waitFor(() => expect(Plotly.react).toHaveBeenCalled())
      expect(onRecordReceived).not.toHaveBeenCalled()
    })

    it('scrolls the x axis while streaming and restores autorange afterwards', async () => {
      const rafCbs: Array<(t: number) => void> = []
      vi.stubGlobal('requestAnimationFrame', (cb: (t: number) => void) => { rafCbs.push(cb); return rafCbs.length })
      const cancel = vi.fn()
      vi.stubGlobal('cancelAnimationFrame', cancel)
      const { user } = await startWith([rec(1, t(0), 1)])
      await waitFor(() => expect(rafCbs.length).toBeGreaterThan(0))
      act(() => rafCbs[0](100))
      expect(Plotly.relayout).toHaveBeenCalledWith(expect.anything(), expect.objectContaining({ 'xaxis.autorange': false }))
      const calls = vi.mocked(Plotly.relayout).mock.calls.length
      act(() => rafCbs[rafCbs.length - 1](110)) // within the frame interval, skipped
      expect(vi.mocked(Plotly.relayout).mock.calls.length).toBe(calls)
      await user.click(await screen.findByRole('button', { name: /стоп/i }))
      expect(cancel).toHaveBeenCalled()
      expect(Plotly.relayout).toHaveBeenLastCalledWith(expect.anything(), { 'xaxis.autorange': true })
    })
  })

  describe('layout', () => {
    it('forwards drag handle props and removes the panel', async () => {
      const user = userEvent.setup()
      const onDragStart = vi.fn()
      const { onRemove } = renderPanel({ dragHandleProps: { onDragStart, draggable: true } })
      expect(screen.getByRole('button', { name: 'Перетащить панель' })).toHaveAttribute('draggable', 'true')
      await user.click(screen.getByRole('button', { name: 'Удалить панель' }))
      expect(onRemove).toHaveBeenCalled()
    })

    it('measures its size with ResizeObserver and resizes the plot', () => {
      const observers: Array<{ cb: () => void; observe: ReturnType<typeof vi.fn>; disconnect: ReturnType<typeof vi.fn> }> = []
      class FakeRO {
        cb: () => void
        observe = vi.fn()
        disconnect = vi.fn()
        constructor(cb: () => void) { this.cb = cb; observers.push(this) }
      }
      vi.stubGlobal('ResizeObserver', FakeRO)
      const onSizeChange = vi.fn()
      const rect = vi.spyOn(HTMLElement.prototype, 'getBoundingClientRect').mockReturnValue({ width: 500.4, height: 300.6 } as DOMRect)
      const { unmount } = renderPanel({ onSizeChange })
      expect(onSizeChange).toHaveBeenCalledWith({ width: 500, height: 301 })
      observers.forEach((o) => act(() => o.cb()))
      expect(Plotly.Plots.resize).toHaveBeenCalled()
      rect.mockReturnValue({ width: 10, height: 10 } as DOMRect)
      observers.forEach((o) => act(() => o.cb()))
      expect(onSizeChange).toHaveBeenLastCalledWith({ width: 280, height: 240 })
      unmount()
      expect(observers.every((o) => o.disconnect.mock.calls.length > 0)).toBe(true)
    })
  })
})
