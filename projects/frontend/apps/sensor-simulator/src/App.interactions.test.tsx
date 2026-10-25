import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { render, screen, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { App } from './App'
import { DEFAULT_SETTINGS, STORAGE_KEY, type SensorConfig } from './domain'

const SENSOR_ID = '11111111-1111-1111-1111-111111111111'
const OTHER_ID = '22222222-2222-2222-2222-222222222222'

function sensor(key: string, overrides: Partial<SensorConfig> = {}): SensorConfig {
  return {
    key,
    label: '',
    sensorId: SENSOR_ID,
    sensorToken: 'token',
    runId: '',
    captureSessionId: '',
    streamSinceId: 0,
    settings: { ...DEFAULT_SETTINGS },
    ...overrides,
  }
}

function seedSensors(sensors: SensorConfig[], selected = sensors[0].key) {
  localStorage.setItem(
    STORAGE_KEY,
    JSON.stringify({ version: 2, sensors, selectedSensorKey: selected }),
  )
}

/** Labels are plain siblings of their inputs, so resolve the control through the label text. */
function control(label: string): HTMLInputElement | HTMLSelectElement {
  const field = screen.getByText(label).parentElement as HTMLElement
  return field.querySelector('input, select') as HTMLInputElement | HTMLSelectElement
}

const tabs = () =>
  Array.from(document.querySelectorAll<HTMLButtonElement>('.sensorTabs > .tab:not(.add)'))
const logText = () => (document.querySelector('.log') as HTMLElement).textContent ?? ''
const events = () => screen.getByPlaceholderText(/events will appear here/i) as HTMLTextAreaElement

const encoder = new TextEncoder()
function sseResponse(...frames: string[]) {
  return new Response(
    new ReadableStream({
      start(controller) {
        for (const frame of frames) controller.enqueue(encoder.encode(frame))
      },
    }),
    { status: 200, headers: { 'Content-Type': 'text/event-stream' } },
  )
}
const telemetryFrame = (id: number) =>
  `event: telemetry\ndata: ${JSON.stringify({ id, timestamp: '2026-01-01T00:00:00Z', raw_value: id })}\n\n`

beforeEach(() => {
  localStorage.clear()
  vi.stubGlobal(
    'fetch',
    vi.fn().mockResolvedValue(new Response(JSON.stringify({ accepted: 0 }), { status: 200 })),
  )
})

afterEach(() => {
  vi.unstubAllGlobals()
})

describe('sensor configuration', () => {
  it('edits identity fields and persists them for the selected sensor', async () => {
    seedSensors([sensor('a', { sensorId: '', sensorToken: '' })])
    const user = userEvent.setup()
    render(<App />)

    await user.type(control('label (optional)'), 'motor-temp')
    await user.type(control('run_id (optional)'), 'run-1')
    await user.type(control('capture_session_id (optional)'), 'cap-1')

    expect(screen.getByRole('button', { name: 'motor-temp' })).toBeInTheDocument()
    await vi.waitFor(
      () => {
        const saved = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? '{}')
        expect(saved.sensors[0]).toMatchObject({
          label: 'motor-temp',
          runId: 'run-1',
          captureSessionId: 'cap-1',
        })
      },
      { timeout: 1500 },
    )
  })

  it('edits every numeric signal setting and both selects', async () => {
    seedSensors([sensor('a')])
    const user = userEvent.setup()
    render(<App />)

    const numeric: Array<[string, string]> = [
      ['seed (deterministic)', '7'],
      ['amplitude', '3'],
      ['period (sec)', '9'],
      ['rate (Hz)', '25'],
      ['batch size (manual)', '12'],
    ]
    for (const [label, value] of numeric) {
      await user.clear(control(label))
      await user.type(control(label), value)
      expect(control(label).value).toBe(value)
    }

    await user.selectOptions(control('signal waveform'), 'pulses')
    expect(control('duty cycle (0..1)')).toBeEnabled()
    await user.clear(control('duty cycle (0..1)'))
    await user.type(control('duty cycle (0..1)'), '0.5')
    expect(control('duty cycle (0..1)').value).toBe('0.5')

    await user.selectOptions(control('signal waveform'), 'saw')
    expect(control('duty cycle (0..1)')).toBeDisabled()
  })

  it.each([
    ['bursts', ['burst every (sec)', 'burst duration (sec)']],
    ['dropout', ['dropout every (sec)', 'dropout duration (sec)']],
    ['late_data', ['late seconds (timestamp - N)']],
    ['out_of_order', ['out-of-order fraction (0..1)']],
  ])('edits the %s scenario parameters', async (scenario, labels) => {
    seedSensors([sensor('a')])
    const user = userEvent.setup()
    render(<App />)

    await user.selectOptions(control('scenario'), scenario)
    for (const label of labels) {
      await user.clear(control(label))
      await user.type(control(label), '4')
      expect(control(label).value).toBe('4')
    }
  })

  it('keeps scenario controls of other scenarios hidden', () => {
    seedSensors([sensor('a')])
    render(<App />)

    expect(screen.queryByText('burst every (sec)')).not.toBeInTheDocument()
    expect(screen.queryByText('dropout every (sec)')).not.toBeInTheDocument()
    expect(screen.queryByText('late seconds (timestamp - N)')).not.toBeInTheDocument()
  })
})

describe('sensor list management', () => {
  it('cannot remove the only sensor or copy settings with a single sensor', () => {
    seedSensors([sensor('a')])
    render(<App />)

    expect(screen.getByRole('button', { name: /remove sensor/i })).toBeDisabled()
    expect(screen.getByRole('button', { name: /copy settings to all/i })).toBeDisabled()
  })

  it('a new sensor inherits the settings of the selected one', async () => {
    seedSensors([sensor('a', { settings: { ...DEFAULT_SETTINGS, amplitude: 77 } })])
    const user = userEvent.setup()
    render(<App />)

    await user.click(screen.getByRole('button', { name: /\+ add sensor/i }))

    expect(tabs()).toHaveLength(2)
    expect(control('amplitude').value).toBe('77')
    expect(control('sensor_id').value).toBe('')
    expect(logText()).toMatch(/add sensor/)
  })

  it('switches between sensors and shows the selected sensor settings', async () => {
    seedSensors([
      sensor('a', { label: 'first', settings: { ...DEFAULT_SETTINGS, amplitude: 1 } }),
      sensor('b', {
        label: 'second',
        sensorId: OTHER_ID,
        settings: { ...DEFAULT_SETTINGS, amplitude: 2 },
      }),
    ])
    const user = userEvent.setup()
    render(<App />)
    expect(control('amplitude').value).toBe('1')

    await user.click(screen.getByRole('button', { name: 'second' }))

    expect(control('amplitude').value).toBe('2')
    expect(control('sensor_id').value).toBe(OTHER_ID)
  })

  it('removes the selected sensor and falls back to the remaining one', async () => {
    seedSensors([sensor('a', { label: 'first' }), sensor('b', { label: 'second' })], 'b')
    const user = userEvent.setup()
    render(<App />)

    await user.click(screen.getByRole('button', { name: /remove sensor/i }))

    expect(tabs()).toHaveLength(1)
    expect(screen.getByRole('button', { name: 'first' })).toBeInTheDocument()
    expect(control('label (optional)').value).toBe('first')
    expect(logText()).toMatch(/remove sensor/)
  })

  it('copies the selected settings to every sensor', async () => {
    seedSensors([
      sensor('a', { label: 'first', settings: { ...DEFAULT_SETTINGS, amplitude: 5 } }),
      sensor('b', { label: 'second', settings: { ...DEFAULT_SETTINGS, amplitude: 9 } }),
    ])
    const user = userEvent.setup()
    render(<App />)

    await user.click(screen.getByRole('button', { name: /copy settings to all/i }))
    await user.click(screen.getByRole('button', { name: 'second' }))

    expect(control('amplitude').value).toBe('5')
  })

  it('shows a ready hint on a complete sensor tab and falls back to a numbered name', () => {
    seedSensors([
      sensor('a', { label: 'ready-one' }),
      sensor('b', { sensorId: '', sensorToken: '', label: '' }),
    ])
    render(<App />)

    expect(screen.getByTitle(/ready — steady \/ sine/)).toBeInTheDocument()
    expect(screen.getByTitle(/missing sensor_id\/token/)).toBeInTheDocument()
    expect(screen.getByText('b')).toBeInTheDocument()
  })

  it('recreates a default sensor when stored sensors are empty', () => {
    localStorage.setItem(
      STORAGE_KEY,
      JSON.stringify({ version: 2, sensors: [], selectedSensorKey: 'gone' }),
    )
    render(<App />)

    expect(tabs()).toHaveLength(1)
  })
})

describe('ingest', () => {
  it('counts accepted readings from the API response and resets the counters', async () => {
    seedSensors([sensor('a', { label: 'one', settings: { ...DEFAULT_SETTINGS, batchSize: 5 } })])
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue(new Response(JSON.stringify({ accepted: 3 }), { status: 200 })),
    )
    const user = userEvent.setup()
    render(<App />)

    await user.click(screen.getByRole('button', { name: /send one batch/i }))

    const kpi = (label: string) => screen.getByText(label).nextElementSibling as HTMLElement
    await vi.waitFor(() => expect(kpi('accepted (from API)')).toHaveTextContent('3'))
    expect(kpi('generated readings')).toHaveTextContent('5')
    expect(logText()).toMatch(/✅ 200 sensor=one/)

    await user.click(screen.getByRole('button', { name: /^reset$/i }))

    expect(kpi('generated readings')).toHaveTextContent('0')
    expect(screen.getByText(/HTTP —/)).toBeInTheDocument()
  })

  it('falls back to the batch size when the API omits the accepted count', async () => {
    seedSensors([sensor('a', { settings: { ...DEFAULT_SETTINGS, batchSize: 4 } })])
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('{}', { status: 200 })))
    const user = userEvent.setup()
    render(<App />)

    await user.click(screen.getByRole('button', { name: /send one batch/i }))

    const accepted = screen.getByText('accepted (from API)').nextElementSibling as HTMLElement
    await vi.waitFor(() => expect(accepted).toHaveTextContent('4'))
  })

  it('counts rejected batches as errors and shows the failing status', async () => {
    seedSensors([sensor('a', { label: 'one' })])
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('invalid token', { status: 401 })))
    const user = userEvent.setup()
    render(<App />)

    await user.click(screen.getByRole('button', { name: /send one batch/i }))

    await vi.waitFor(() => expect(screen.getByText(/HTTP 401/)).toBeInTheDocument())
    const errors = screen.getByText('errors').nextElementSibling as HTMLElement
    expect(errors).toHaveTextContent('1')
    expect(logText()).toMatch(/❌ 401 sensor=one: invalid token/)
    expect(document.querySelector('.pill')).toHaveClass('bad')
  })

  it('sends a batch for every ready sensor and skips incomplete ones', async () => {
    seedSensors([
      sensor('a'),
      sensor('b', { sensorId: OTHER_ID }),
      sensor('c', { sensorId: '', sensorToken: '' }),
    ])
    const fetchMock = vi.fn().mockResolvedValue(new Response('{}', { status: 200 }))
    vi.stubGlobal('fetch', fetchMock)
    const user = userEvent.setup()
    render(<App />)
    expect(screen.getByText('sensors (active/total)').nextElementSibling).toHaveTextContent('2/3')

    await user.click(screen.getByRole('button', { name: /send one batch/i }))

    await vi.waitFor(() => expect(fetchMock).toHaveBeenCalledTimes(2))
  })

  it('runs continuously until stopped and reports the effective rate', async () => {
    seedSensors([sensor('a', { label: 'one', settings: { ...DEFAULT_SETTINGS, rateHz: 50 } })])
    const fetchMock = vi.fn().mockResolvedValue(new Response('{}', { status: 200 }))
    vi.stubGlobal('fetch', fetchMock)
    const user = userEvent.setup()
    render(<App />)

    await user.click(screen.getByRole('button', { name: /start ingest/i }))

    await vi.waitFor(() => expect(fetchMock).toHaveBeenCalled())
    expect(screen.getByText('state').nextElementSibling).toHaveTextContent('running')
    expect(screen.getByText(/one: 50\.0 Hz/)).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /start ingest/i })).toBeDisabled()

    await user.click(screen.getByRole('button', { name: /stop ingest/i }))

    expect(screen.getByText('state').nextElementSibling).toHaveTextContent('idle')
    expect(logText()).toMatch(/⏹ stop/)
  })

  it('does not start without a ready sensor', () => {
    seedSensors([sensor('a', { sensorToken: '' })])
    render(<App />)

    expect(screen.getByRole('button', { name: /start ingest/i })).toBeDisabled()
    expect(screen.getByRole('button', { name: /send one batch/i })).toBeDisabled()
  })
})

describe('telemetry stream', () => {
  const routeFetch = (stream: () => Response) =>
    vi.fn().mockImplementation((input: RequestInfo | URL) =>
      String(input).includes('/stream')
        ? Promise.resolve(stream())
        : Promise.resolve(new Response('{}', { status: 200 })),
    )

  it('streams events, tracks since_id, locks the sensor list and disconnects', async () => {
    seedSensors([sensor('a', { label: 'one' })])
    const fetchMock = routeFetch(() => sseResponse(telemetryFrame(5), telemetryFrame(6)))
    vi.stubGlobal('fetch', fetchMock)
    const user = userEvent.setup()
    render(<App />)

    await user.click(screen.getByRole('button', { name: /^connect$/i }))

    await vi.waitFor(() => expect(events().value).toContain('"id":6'))
    expect(events().value.split('\n')).toHaveLength(2)
    expect(screen.getByDisplayValue('6')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /\+ add sensor/i })).toBeDisabled()
    expect(screen.getByRole('button', { name: /^connect$/i })).toBeDisabled()
    expect(logText()).toMatch(/stream connect sensor=one since_id=0/)
    const url = new URL(String(fetchMock.mock.calls.find(([u]) => String(u).includes('/stream'))![0]))
    expect(url.searchParams.get('sensor_id')).toBe(SENSOR_ID)

    await user.click(screen.getByRole('button', { name: /disconnect/i }))

    expect(screen.getByRole('button', { name: /\+ add sensor/i })).toBeEnabled()
    expect(logText()).toMatch(/stream disconnect/)

    await user.click(screen.getByRole('button', { name: /clear events/i }))
    expect(events().value).toBe('')
  })

  it('resumes from the since_id edited by the user', async () => {
    seedSensors([sensor('a')])
    const fetchMock = routeFetch(() => sseResponse())
    vi.stubGlobal('fetch', fetchMock)
    const user = userEvent.setup()
    render(<App />)
    const sinceId = control('since_id')

    await user.clear(sinceId)
    await user.type(sinceId, '42')
    await user.click(screen.getByRole('button', { name: /^connect$/i }))

    await vi.waitFor(() => {
      const call = fetchMock.mock.calls.find(([u]) => String(u).includes('/stream'))
      expect(new URL(String(call![0])).searchParams.get('since_id')).toBe('42')
    })
  })

  it('logs a rejected stream handshake', async () => {
    seedSensors([sensor('a')])
    vi.stubGlobal(
      'fetch',
      routeFetch(() => new Response('token expired', { status: 401, statusText: 'Unauthorized' })),
    )
    const user = userEvent.setup()
    render(<App />)

    await user.click(screen.getByRole('button', { name: /^connect$/i }))

    await vi.waitFor(() => expect(logText()).toMatch(/stream error: HTTP 401: token expired/))
  })

  it('only shows the selected sensor on the stream card', () => {
    seedSensors([sensor('a', { label: 'first' }), sensor('b', { label: 'second' })], 'b')
    render(<App />)

    const card = screen.getByRole('heading', { name: /sse stream/i }).closest('.card') as HTMLElement
    expect(within(card).getByDisplayValue('second')).toBeInTheDocument()
  })
})
