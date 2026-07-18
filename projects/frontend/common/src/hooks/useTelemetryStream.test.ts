import { describe, it, expect, vi, afterEach } from 'vitest'
import { act, renderHook, waitFor } from '@testing-library/react'
import { useTelemetryStream } from './useTelemetryStream'
import { createControllableSSEStream, fakeResponse } from '../testUtils/sse'
import type { TelemetryStreamRecord, TelemetryStreamOpenParams } from '../types/telemetry'

function makeRecord(overrides: Partial<TelemetryStreamRecord> = {}): TelemetryStreamRecord {
  return {
    id: 1,
    sensor_id: 's1',
    project_id: 'p1',
    timestamp: '2024-01-01T00:00:01.000Z',
    raw_value: 1,
    physical_value: 1,
    run_id: null,
    capture_session_id: null,
    meta: {},
    ...overrides,
  }
}

/** Mocked opener: each call spins up a fresh controllable SSE stream and wires
 * the AbortSignal to it, mimicking how a real aborted fetch would error the reader. */
function makeOpenMock() {
  const streams: ReturnType<typeof createControllableSSEStream>[] = []
  const open = vi.fn(async (params: TelemetryStreamOpenParams) => {
    const s = createControllableSSEStream()
    streams.push(s)
    const abortError = () => s.error(Object.assign(new Error('Aborted'), { name: 'AbortError' }))
    if (params.signal.aborted) abortError()
    else params.signal.addEventListener('abort', abortError)
    return { response: fakeResponse(s.stream) }
  })
  return { open, streams }
}

const FAST_BACKOFF = { initialDelayMs: 5, maxDelayMs: 5, jitterRatio: 0 }

describe('useTelemetryStream', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('does not connect until start() is called', () => {
    const { open } = makeOpenMock()
    const { result } = renderHook(() => useTelemetryStream('s1', { open }))
    expect(open).not.toHaveBeenCalled()
    expect(result.current.status).toBe('idle')
  })

  it('calls open() once with the initial sinceTs/sinceId/idleTimeoutSeconds', async () => {
    const { open } = makeOpenMock()
    const { result } = renderHook(() =>
      useTelemetryStream('s1', {
        open,
        initialSinceTs: '2024-01-01T00:00:00.000Z',
        initialSinceId: 5,
        idleTimeoutSeconds: 15,
      }),
    )

    act(() => result.current.start())

    await waitFor(() => expect(open).toHaveBeenCalledTimes(1))
    expect(open).toHaveBeenCalledWith(
      expect.objectContaining({
        sensorId: 's1',
        sinceTs: '2024-01-01T00:00:00.000Z',
        sinceId: 5,
        idleTimeoutSeconds: 15,
      }),
    )
  })

  it('caps buffered points at bufferSize, dropping the oldest', async () => {
    const { open, streams } = makeOpenMock()
    const { result } = renderHook(() => useTelemetryStream('s1', { open, bufferSize: 3 }))

    act(() => result.current.start())
    await waitFor(() => expect(streams).toHaveLength(1))

    act(() => {
      for (let i = 1; i <= 5; i++) {
        streams[0].push('telemetry', makeRecord({ id: i, timestamp: `2024-01-01T00:00:0${i}.000Z` }))
      }
    })

    await waitFor(() => expect(result.current.points).toHaveLength(3))
    expect(result.current.points.map((p) => p.id)).toEqual([3, 4, 5])
  })

  it('stop() aborts immediately and does not schedule a reconnect', async () => {
    const { open, streams } = makeOpenMock()
    const { result } = renderHook(() =>
      useTelemetryStream('s1', { open, backoff: FAST_BACKOFF }),
    )

    act(() => result.current.start())
    await waitFor(() => expect(streams).toHaveLength(1))

    act(() => result.current.stop())
    await waitFor(() => expect(result.current.status).toBe('stopped'))

    // Give any (incorrectly) scheduled reconnect timer a chance to fire.
    await new Promise((r) => setTimeout(r, 50))
    expect(open).toHaveBeenCalledTimes(1)
  })

  it('reconnects after an unplanned clean stream end, resuming from the last received cursor', async () => {
    const { open, streams } = makeOpenMock()
    const { result } = renderHook(() =>
      useTelemetryStream('s1', {
        open,
        backoff: FAST_BACKOFF,
        initialSinceTs: '2024-01-01T00:00:00.000Z',
        initialSinceId: 0,
      }),
    )

    act(() => result.current.start())
    await waitFor(() => expect(streams).toHaveLength(1))

    act(() => {
      streams[0].push('telemetry', makeRecord({ id: 42, timestamp: '2024-01-01T00:00:42.000Z' }))
    })
    await waitFor(() => expect(result.current.lastRecord?.id).toBe(42))

    act(() => streams[0].close())

    await waitFor(() => expect(streams).toHaveLength(2))
    expect(open).toHaveBeenNthCalledWith(
      2,
      expect.objectContaining({ sinceTs: '2024-01-01T00:00:42.000Z', sinceId: 42 }),
    )
  })

  it('reuses the initial cursor on reconnect if no records were ever received', async () => {
    const { open, streams } = makeOpenMock()
    const { result } = renderHook(() =>
      useTelemetryStream('s1', {
        open,
        backoff: FAST_BACKOFF,
        initialSinceTs: '2024-01-01T00:00:00.000Z',
        initialSinceId: 7,
      }),
    )

    act(() => result.current.start())
    await waitFor(() => expect(streams).toHaveLength(1))

    act(() => streams[0].close())

    await waitFor(() => expect(streams).toHaveLength(2))
    expect(open).toHaveBeenNthCalledWith(
      2,
      expect.objectContaining({ sinceTs: '2024-01-01T00:00:00.000Z', sinceId: 7 }),
    )
  })

  it('follows the same reconnect path when open() itself rejects', async () => {
    let call = 0
    const streams: ReturnType<typeof createControllableSSEStream>[] = []
    const open = vi.fn(async () => {
      call += 1
      if (call === 1) throw new Error('network down')
      const s = createControllableSSEStream()
      streams.push(s)
      return { response: fakeResponse(s.stream) }
    })

    const { result } = renderHook(() => useTelemetryStream('s1', { open, backoff: FAST_BACKOFF }))
    act(() => result.current.start())

    await waitFor(() => expect(open).toHaveBeenCalledTimes(2))
    await waitFor(() => expect(result.current.status).toBe('streaming'))
  })

  it('goes straight to a terminal error with autoReconnect disabled', async () => {
    const { open, streams } = makeOpenMock()
    const { result } = renderHook(() =>
      useTelemetryStream('s1', { open, autoReconnect: false }),
    )

    act(() => result.current.start())
    await waitFor(() => expect(streams).toHaveLength(1))

    act(() => streams[0].close())

    await waitFor(() => expect(result.current.status).toBe('error'))
    await new Promise((r) => setTimeout(r, 20))
    expect(open).toHaveBeenCalledTimes(1)
  })

  it('stops retrying once maxAttempts consecutive failures are reached', async () => {
    // Attempt count resets on any successful connection (standard backoff
    // behavior), so to reach maxAttempts, open() must fail every time.
    const open = vi.fn(async () => {
      throw new Error('network down')
    })
    const onError = vi.fn()
    const { result } = renderHook(() =>
      useTelemetryStream('s1', {
        open,
        backoff: { ...FAST_BACKOFF, maxAttempts: 2 },
        onError,
      }),
    )

    act(() => result.current.start())

    await waitFor(() => expect(result.current.status).toBe('error'))
    expect(open).toHaveBeenCalledTimes(3) // initial attempt + 2 retries
    expect(onError).toHaveBeenLastCalledWith(expect.any(Error), { willRetry: false, attempt: 2 })
  })

  it('gives up after the default maxAttempts (10) instead of retrying forever', async () => {
    // A permanent failure (401/403/404, a backend "error" event) must eventually
    // surface a terminal error rather than reconnecting indefinitely.
    const open = vi.fn(async () => {
      throw new Error('forbidden')
    })
    const onError = vi.fn()
    const { result } = renderHook(() =>
      useTelemetryStream('s1', { open, backoff: FAST_BACKOFF, onError }),
    )

    act(() => result.current.start())

    await waitFor(() => expect(result.current.status).toBe('error'), { timeout: 3000 })
    expect(open).toHaveBeenCalledTimes(11) // initial attempt + 10 retries
    expect(onError).toHaveBeenLastCalledWith(expect.any(Error), { willRetry: false, attempt: 10 })
  })

  it('does not crash on malformed JSON in a telemetry event', async () => {
    const { open, streams } = makeOpenMock()
    const { result } = renderHook(() => useTelemetryStream('s1', { open }))

    act(() => result.current.start())
    await waitFor(() => expect(streams).toHaveLength(1))

    act(() => {
      streams[0].push('telemetry', undefined) // JSON.parse('undefined'.toString()) -> throws
      streams[0].pushRaw('event: telemetry\ndata: {not json\n\n')
      streams[0].push('telemetry', makeRecord({ id: 9 }))
    })

    await waitFor(() => expect(result.current.lastRecord?.id).toBe(9))
    expect(result.current.points).toHaveLength(1)
  })

  it('ignores non-telemetry SSE events', async () => {
    const { open, streams } = makeOpenMock()
    const { result } = renderHook(() => useTelemetryStream('s1', { open }))

    act(() => result.current.start())
    await waitFor(() => expect(streams).toHaveLength(1))

    act(() => {
      streams[0].push('heartbeat', { ping: true })
      streams[0].push('telemetry', makeRecord({ id: 1 }))
    })

    await waitFor(() => expect(result.current.points).toHaveLength(1))
    expect(result.current.points[0].id).toBe(1)
  })

  it('cancels the in-flight request and any pending reconnect timer on unmount', async () => {
    const { open, streams } = makeOpenMock()
    const { result, unmount } = renderHook(() =>
      useTelemetryStream('s1', { open, backoff: FAST_BACKOFF }),
    )

    act(() => result.current.start())
    await waitFor(() => expect(streams).toHaveLength(1))

    unmount()
    // Unmount already aborts the in-flight controller (which errors the mock
    // stream via its abort listener) — no further action needed here.

    await new Promise((r) => setTimeout(r, 50))
    expect(open).toHaveBeenCalledTimes(1)
  })

  it('treats a server-sent "error" event as a stream failure, not a clean end', async () => {
    const { open, streams } = makeOpenMock()
    const onError = vi.fn()
    const { result } = renderHook(() =>
      useTelemetryStream('s1', { open, backoff: FAST_BACKOFF, onError }),
    )

    act(() => result.current.start())
    await waitFor(() => expect(streams).toHaveLength(1))

    act(() => {
      streams[0].pushRaw('event: error\ndata: sensor disconnected\n\n')
    })

    await waitFor(() => expect(onError).toHaveBeenCalledTimes(1))
    expect(onError).toHaveBeenCalledWith(
      expect.objectContaining({ message: 'sensor disconnected' }),
      { willRetry: true, attempt: 1 },
    )
    await waitFor(() => expect(streams).toHaveLength(2))
  })

  it('clear() resets points, lastRecord, and cursor back to the initial values', async () => {
    const { open, streams } = makeOpenMock()
    const { result } = renderHook(() =>
      useTelemetryStream('s1', {
        open,
        initialSinceTs: '2024-01-01T00:00:00.000Z',
        initialSinceId: 3,
      }),
    )

    act(() => result.current.start())
    await waitFor(() => expect(streams).toHaveLength(1))

    act(() => {
      streams[0].push('telemetry', makeRecord({ id: 99, timestamp: '2024-01-01T00:01:39.000Z' }))
    })
    await waitFor(() => expect(result.current.lastRecord?.id).toBe(99))

    act(() => result.current.clear())

    expect(result.current.points).toEqual([])
    expect(result.current.lastRecord).toBeNull()
    expect(result.current.cursor).toEqual({ sinceTs: '2024-01-01T00:00:00.000Z', sinceId: 3 })
  })
})
