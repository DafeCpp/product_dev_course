import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor, fireEvent } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import Plotly from 'plotly.js-dist-min'
import TelemetryPanel from './TelemetryPanel'
import type { Sensor } from '../types'

vi.mock('../api/client', () => ({ telemetryApi: { stream: vi.fn() } }))
vi.mock('plotly.js-dist-min', () => ({
  default: { react: vi.fn(), purge: vi.fn(), relayout: vi.fn(), Plots: { resize: vi.fn() } },
}))

type Rec = ReturnType<typeof rec>
type FakeStream = { status: string; points: Rec[]; lastRecord: Rec | null }
const streams: Record<string, FakeStream> = {}

vi.mock('frontend-common', () => ({
  useTelemetryStream: (sensorId: string) => {
    const s =
      streams[sensorId] ?? (streams[sensorId] = { status: 'idle', points: [], lastRecord: null })
    return { ...s, start: vi.fn(), stop: vi.fn(), clear: vi.fn() }
  },
}))

function sensor(id: string): Sensor {
  return {
    id,
    project_id: 'p1',
    name: `Sensor ${id}`,
    type: 'temperature',
    input_unit: 'raw',
    display_unit: 'C',
    status: 'active',
    connection_status: 'online',
    token_preview: null,
    last_heartbeat: null,
    active_profile_id: null,
    calibration_notes: null,
    created_at: '2026-01-01T00:00:00Z',
    updated_at: '2026-01-01T00:00:00Z',
  } as Sensor
}

const sensors = [sensor('s1'), sensor('s2')]
const KEY = 'telemetry_panel_state_pan'

function rec(id: number, ts: string, phys: number) {
  return {
    id,
    sensor_id: 's1',
    project_id: 'p1',
    timestamp: ts,
    physical_value: phys,
    raw_value: phys,
    run_id: null,
    capture_session_id: null,
    meta: {},
  }
}

function renderPanel() {
  return render(<TelemetryPanel panelId="pan" title="Panel" sensors={sensors} onRemove={vi.fn()} />)
}

describe('TelemetryPanel (branches)', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    window.localStorage.clear()
    for (const k of Object.keys(streams)) delete streams[k]
    vi.spyOn(console, 'debug').mockImplementation(() => {})
  })
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('keeps all points when the latest timestamp is before the epoch and handles unordered records', async () => {
    const user = userEvent.setup()
    window.localStorage.setItem(KEY, JSON.stringify({ selectedSensorIds: ['s1'], timeWindowSeconds: 5 }))
    const points = [rec(1, '1960-01-01T00:10:00Z', 1), rec(2, '1960-01-01T00:00:00Z', 2)]
    streams.s1 = { status: 'streaming', points, lastRecord: points[1] }
    renderPanel()
    await user.click(await screen.findByRole('button', { name: /старт/i }))
    await waitFor(() => expect(screen.getByText('last=2.000')).toBeInTheDocument())
    const lastCall = vi.mocked(Plotly.react).mock.calls.at(-1) as unknown as [unknown, Array<{ y: number[] }>]
    expect(lastCall[1][0].y).toEqual([1, 2])
  })

  it('keeps records of still-selected sensors when another sensor is added', async () => {
    const user = userEvent.setup()
    window.localStorage.setItem(KEY, JSON.stringify({ selectedSensorIds: ['s1'] }))
    const t = new Date().toISOString()
    const points = [rec(1, t, 7)]
    streams.s1 = { status: 'streaming', points, lastRecord: points[0] }
    const { container } = renderPanel()
    await user.click(await screen.findByRole('button', { name: /старт/i }))
    expect(await screen.findByText('last=7.000')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: /настройки/i }))
    const native = container.querySelector<HTMLSelectElement>('.md-select__native')
    expect(native).not.toBeNull()
    fireEvent.change(native as HTMLSelectElement, { target: { value: 's2' } })
    expect(await screen.findAllByText('Sensor s2')).not.toHaveLength(0)
    expect(screen.getByText('last=7.000')).toBeInTheDocument()
  })

  it('ignores an empty sensor selection from the native select', async () => {
    const user = userEvent.setup()
    const { container } = renderPanel()
    await user.click(screen.getByRole('button', { name: /настройки/i }))
    const native = container.querySelector<HTMLSelectElement>('.md-select__native') as HTMLSelectElement
    fireEvent.change(native, { target: { value: '' } })
    expect(screen.getByText('Сенсоры не выбраны')).toBeInTheDocument()
    expect(native.value).toBe('')
  })
})
