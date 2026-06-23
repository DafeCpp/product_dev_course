import { describe, it, expect, beforeEach, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { App } from './App'

const STORAGE_KEY = 'sensor-simulator:params:v1'

beforeEach(() => {
  localStorage.clear()
  // The simulator polls a streaming endpoint via fetch; avoid real network calls.
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(
    new Response(JSON.stringify({ accepted: 0 }), { status: 200 })
  ))
})

// Smoke tests — render-and-interact only. Real ingest, streaming SSE,
// and RNG-driven scenario timing are out of scope.

describe('sensor-simulator <App>', () => {
  it('renders the main UI with header, status pill, and add-sensor button', () => {
    render(<App />)

    expect(screen.getByRole('heading', { name: /sensor simulator/i })).toBeInTheDocument()
    expect(screen.getByText(/HTTP/i)).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /\+ add sensor/i })).toBeInTheDocument()
    // Default sensor exists with the "missing sensor_id/token" warn-state title.
    expect(screen.getByTitle(/missing sensor_id\/token/i)).toBeInTheDocument()
  })

  it('shows "incomplete" status for a fresh sensor without sensor_id/token', () => {
    render(<App />)
    const statusInput = screen.getByDisplayValue(/incomplete/i) as HTMLInputElement
    expect(statusInput).toBeInTheDocument()
    expect(statusInput.readOnly).toBe(true)
  })

  it('disables "Start ingest" until at least one sensor is ready', () => {
    render(<App />)
    expect(screen.getByRole('button', { name: /start ingest/i })).toBeDisabled()
  })

  it('enables "Start ingest" once a sensor has both sensor_id and token', async () => {
    const user = userEvent.setup()
    render(<App />)

    await user.type(
      screen.getByPlaceholderText(/xxxxxxxx-xxxx-xxxx/i),
      '11111111-1111-1111-1111-111111111111'
    )
    await user.type(screen.getByPlaceholderText(/^token\.\.\.$/i), 'fake-token')

    expect(screen.getByRole('button', { name: /start ingest/i })).not.toBeDisabled()
    expect(screen.getByDisplayValue(/^ready$/i)).toBeInTheDocument()
  })

  it('adds a new sensor tab when "+ Add sensor" is clicked', async () => {
    const user = userEvent.setup()
    render(<App />)

    // Count sensor tabs by their warn/ok class — initially exactly one.
    const initialTabs = document.querySelectorAll('.sensorTabs > .tab:not(.add)')
    expect(initialTabs.length).toBe(1)

    await user.click(screen.getByRole('button', { name: /\+ add sensor/i }))

    const afterTabs = document.querySelectorAll('.sensorTabs > .tab:not(.add)')
    expect(afterTabs.length).toBe(2)
  })

  it('persists sensor state to localStorage on changes', async () => {
    const user = userEvent.setup()
    render(<App />)

    await user.type(
      screen.getByPlaceholderText(/xxxxxxxx-xxxx-xxxx/i),
      'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa'
    )

    await vi.waitFor(() => {
      const raw = localStorage.getItem(STORAGE_KEY)
      expect(raw).not.toBeNull()
      const parsed = JSON.parse(raw!) as { sensors: Array<{ sensorId: string }> }
      expect(parsed.sensors[0].sensorId).toBe('aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa')
    }, { timeout: 1500 })
  })

  it('restores sensor state from localStorage on mount', () => {
    localStorage.setItem(
      STORAGE_KEY,
      JSON.stringify({
        version: 2,
        sensors: [
          {
            key: 'sensor_test',
            label: 'restored-label',
            sensorId: '22222222-2222-2222-2222-222222222222',
            sensorToken: 'persisted-token',
            runId: '',
            captureSessionId: '',
            streamSinceId: 0,
            settings: {
              scenario: 'steady',
              rateHz: 10,
              batchSize: 50,
              seed: 7,
              burstEverySec: 12,
              burstDurationSec: 3,
              dropoutEverySec: 18,
              dropoutDurationSec: 6,
              lateSeconds: 3600,
              outOfOrderFraction: 0.2,
              waveform: 'sine',
              amplitude: 10,
              periodSec: 5,
              dutyCycle: 0.1,
            },
          },
        ],
        selectedSensorKey: 'sensor_test',
      })
    )

    render(<App />)

    expect(screen.getByRole('button', { name: /restored-label/i })).toBeInTheDocument()
    expect(
      screen.getByDisplayValue('22222222-2222-2222-2222-222222222222')
    ).toBeInTheDocument()
    expect(screen.getByDisplayValue('persisted-token')).toBeInTheDocument()
    expect(screen.getByDisplayValue(/^ready$/i)).toBeInTheDocument()
  })

  it('changes the scenario via the select control', async () => {
    const user = userEvent.setup()
    render(<App />)

    // Find by initial value (default scenario is 'steady').
    const scenarioSelect = screen.getByDisplayValue('steady stream') as HTMLSelectElement
    expect(scenarioSelect.tagName).toBe('SELECT')

    await user.selectOptions(scenarioSelect, 'bursts')
    expect(scenarioSelect.value).toBe('bursts')
  })

  it('Reset button clears the log/counters and emits a reset entry', async () => {
    const user = userEvent.setup()
    render(<App />)

    await user.click(screen.getByRole('button', { name: /^reset$/i }))

    // The log is rendered as <div class="log">; check its text content.
    const logDiv = document.querySelector('.log') as HTMLElement
    expect(logDiv.textContent).toMatch(/🧹 reset counters\/log/i)
  })

  it('sensor tab gets the "ok" class once ready', async () => {
    const user = userEvent.setup()
    render(<App />)

    const sensorTab = document.querySelector('.sensorTabs > .tab:not(.add)') as HTMLElement
    expect(sensorTab.className).toMatch(/warn/)
    expect(sensorTab.className).not.toMatch(/\bok\b/)

    await user.type(
      screen.getByPlaceholderText(/xxxxxxxx-xxxx-xxxx/i),
      '33333333-3333-3333-3333-333333333333'
    )
    await user.type(screen.getByPlaceholderText(/^token\.\.\.$/i), 'tok')

    expect(sensorTab.className).toMatch(/\bok\b/)
  })

  it('renders the streaming card with disabled Disconnect button initially', () => {
    render(<App />)
    expect(screen.getByRole('button', { name: /disconnect/i })).toBeDisabled()
  })
})

describe('sensor-simulator localStorage corruption', () => {
  it('falls back to a fresh default when stored payload is malformed', () => {
    localStorage.setItem(STORAGE_KEY, '{not valid json')
    render(<App />)
    // Despite corrupted storage, exactly one default sensor tab is rendered.
    const tabs = document.querySelectorAll('.sensorTabs > .tab:not(.add)')
    expect(tabs.length).toBe(1)
  })

  it('falls back when stored payload has wrong shape', () => {
    localStorage.setItem(STORAGE_KEY, JSON.stringify({ version: 999, hello: 'world' }))
    render(<App />)
    const tabs = document.querySelectorAll('.sensorTabs > .tab:not(.add)')
    expect(tabs.length).toBe(1)
  })
})

describe('sensor-simulator <App> — header status pill', () => {
  it('shows HTTP — when no requests have been made', () => {
    render(<App />)
    expect(screen.getByText(/HTTP —/i)).toBeInTheDocument()
  })
})

describe('sensor-simulator ingest flow', () => {
  // These tests exercise the ingest pipeline (buildReadings, sendBatch,
  // waveformValue, mulberry32 RNG, payload assembly) by clicking
  // "Send one batch" once a sensor is ready and inspecting the fetch mock.

  it('Stop ingest is disabled when nothing is running', async () => {
    const user = userEvent.setup()
    render(<App />)
    expect(screen.getByRole('button', { name: /^stop ingest$/i })).toBeDisabled()
    // Even after typing identity, Stop stays disabled until Start is clicked.
    await user.type(
      screen.getByPlaceholderText(/xxxxxxxx-xxxx-xxxx/i),
      '44444444-4444-4444-4444-444444444444'
    )
    await user.type(screen.getByPlaceholderText(/^token\.\.\.$/i), 'tok')
    expect(screen.getByRole('button', { name: /^stop ingest$/i })).toBeDisabled()
  })

  it('Send one batch posts a well-formed payload to /telemetry', async () => {
    const fetchMock = vi.fn().mockResolvedValue(
      new Response(JSON.stringify({ accepted: 50 }), { status: 200 })
    )
    vi.stubGlobal('fetch', fetchMock)

    const user = userEvent.setup()
    render(<App />)

    await user.type(
      screen.getByPlaceholderText(/xxxxxxxx-xxxx-xxxx/i),
      '55555555-5555-5555-5555-555555555555'
    )
    await user.type(screen.getByPlaceholderText(/^token\.\.\.$/i), 'bearer-tok-xyz')

    await user.click(screen.getByRole('button', { name: /send one batch/i }))

    await vi.waitFor(() => {
      expect(fetchMock).toHaveBeenCalledTimes(1)
    })

    const [url, init] = fetchMock.mock.calls[0]
    expect(url).toMatch(/\/telemetry\/api\/v1\/telemetry$/)
    expect(init.method).toBe('POST')
    expect(init.headers['Authorization']).toBe('Bearer bearer-tok-xyz')
    expect(init.headers['Content-Type']).toBe('application/json')

    const body = JSON.parse(init.body as string) as {
      sensor_id: string
      readings: Array<{ timestamp: string; raw_value: number }>
    }
    expect(body.sensor_id).toBe('55555555-5555-5555-5555-555555555555')
    expect(Array.isArray(body.readings)).toBe(true)
    expect(body.readings.length).toBe(50) // default batch size
    // readings have ISO timestamps and numeric raw_value.
    expect(body.readings[0].timestamp).toMatch(/^\d{4}-\d{2}-\d{2}T/)
    expect(typeof body.readings[0].raw_value).toBe('number')
  })

  it('updates HTTP status pill to 200 after a successful send', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(
      new Response(JSON.stringify({ accepted: 1 }), { status: 200 })
    ))

    const user = userEvent.setup()
    render(<App />)

    await user.type(
      screen.getByPlaceholderText(/xxxxxxxx-xxxx-xxxx/i),
      '66666666-6666-6666-6666-666666666666'
    )
    await user.type(screen.getByPlaceholderText(/^token\.\.\.$/i), 't')

    await user.click(screen.getByRole('button', { name: /send one batch/i }))

    await vi.waitFor(() => {
      expect(screen.getByText(/HTTP 200/i)).toBeInTheDocument()
    })
  })

  it('records a network error and increments the error counter', async () => {
    vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new TypeError('network down')))

    const user = userEvent.setup()
    render(<App />)

    await user.type(
      screen.getByPlaceholderText(/xxxxxxxx-xxxx-xxxx/i),
      '77777777-7777-7777-7777-777777777777'
    )
    await user.type(screen.getByPlaceholderText(/^token\.\.\.$/i), 't')

    await user.click(screen.getByRole('button', { name: /send one batch/i }))

    await vi.waitFor(() => {
      // The log should mention the network error.
      const logDiv = document.querySelector('.log') as HTMLElement
      expect(logDiv.textContent).toMatch(/network down|net error/i)
    })
  })
})
