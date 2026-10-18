import { describe, it, expect, vi, beforeEach } from 'vitest'
import { renderHook, act } from '@testing-library/react'
import useTelemetryHistory from './useTelemetryHistory'

const queryState: Record<string, unknown> = {}
const refetch = vi.fn()
const lastArgs: { current: Record<string, unknown> | null } = { current: null }

vi.mock('frontend-common', () => ({
  useTelemetryQuery: (args: Record<string, unknown>) => {
    lastArgs.current = args
    return { refetch, ...queryState }
  },
}))
vi.mock('../api/client', () => ({
  telemetryApi: { query: vi.fn(), aggregated: vi.fn() },
}))

const sensors = [
  { id: 's1', name: 'Thermo', type: 'temperature' },
  { id: 's2', name: 'Baro', type: 'pressure' },
  { id: 's3', name: 'Gyro', type: 'imu' },
] as never[]

function pt(id: number, sensor: string, ts: string, phys = 1, raw = 2) {
  return { id, sensor_id: sensor, timestamp: ts, physical_value: phys, raw_value: raw }
}

describe('useTelemetryHistory', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    window.localStorage.clear()
    for (const k of Object.keys(queryState)) delete queryState[k]
    Object.assign(queryState, { mode: 'raw', points: [], buckets: [], isFetching: false, loadedCount: 0, error: null, wasTruncated: false })
  })

  it('starts with defaults and persists state after the initial load', () => {
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    expect(result.current.valueMode).toBe('physical')
    expect(result.current.includeLate).toBe(true)
    expect(result.current.maxPoints).toBe(5000)
    expect(result.current.order).toBe('asc')
    expect(result.current.hasData).toBe(false)
    expect(JSON.parse(window.localStorage.getItem('telemetry_history_state')!)).toMatchObject({ valueMode: 'physical', maxPoints: 5000 })
  })

  it('restores a valid persisted state and clamps values', () => {
    window.localStorage.setItem('telemetry_history_state', JSON.stringify({
      captureSessionId: 'cs1', sensorIds: ['s1', 5, 's2'], valueMode: 'raw', includeLate: false, maxPoints: 99999.4, order: 'desc',
    }))
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    expect(result.current.captureSessionId).toBe('cs1')
    expect(result.current.sensorIds).toEqual(['s1', 's2'])
    expect(result.current.valueMode).toBe('raw')
    expect(result.current.includeLate).toBe(false)
    expect(result.current.maxPoints).toBe(20000)
    expect(result.current.order).toBe('desc')
  })

  it('clamps too-small persisted maxPoints and ignores bad types', () => {
    window.localStorage.setItem('telemetry_history_state', JSON.stringify({
      captureSessionId: 7, sensorIds: 'nope', valueMode: 'weird', includeLate: 'yes', maxPoints: 5, order: 'sideways',
    }))
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    expect(result.current.maxPoints).toBe(100)
    expect(result.current.captureSessionId).toBe('')
    expect(result.current.sensorIds).toEqual([])
    expect(result.current.valueMode).toBe('physical')
    expect(result.current.order).toBe('asc')
  })

  it('ignores a non-finite persisted maxPoints and malformed JSON', () => {
    window.localStorage.setItem('telemetry_history_state', '{broken')
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    expect(result.current.maxPoints).toBe(5000)
  })

  it('adds, de-duplicates, removes, clears and bulk-adds sensors', () => {
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    act(() => result.current.add('s1'))
    act(() => result.current.add('s1'))
    act(() => result.current.add(''))
    expect(result.current.sensorIds).toEqual(['s1'])
    expect(result.current.availableSensors.map((s) => s.id)).toEqual(['s2', 's3'])
    act(() => result.current.addAll())
    expect(result.current.sensorIds).toEqual(['s1', 's2', 's3'])
    act(() => result.current.remove('s2'))
    expect(result.current.sensorIds).toEqual(['s1', 's3'])
    act(() => result.current.clear())
    expect(result.current.sensorIds).toEqual([])
  })

  it('reports an error when more than 50 sensors are added', () => {
    const many = Array.from({ length: 51 }, (_, i) => ({ id: `x${i}`, name: `n${i}`, type: 't' })) as never[]
    const { result } = renderHook(() => useTelemetryHistory(many))
    act(() => result.current.addAll())
    expect(result.current.sensorIds).toHaveLength(50)
    act(() => result.current.add('x50'))
    expect(result.current.error).toBe('Можно выбрать не более 50 сенсоров')
    expect(result.current.sensorIds).toHaveLength(50)
  })

  it('filters available sensors by name, type and id', () => {
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    act(() => result.current.setFilter('  PRESS '))
    expect(result.current.filteredSensors.map((s) => s.id)).toEqual(['s2'])
    act(() => result.current.setFilter('s3'))
    expect(result.current.filteredSensors.map((s) => s.id)).toEqual(['s3'])
    act(() => result.current.setFilter(''))
    expect(result.current.filteredSensors).toHaveLength(3)
  })

  it('does not load without a capture session; loads and clears the error otherwise', () => {
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    act(() => result.current.load())
    expect(refetch).not.toHaveBeenCalled()
    act(() => result.current.setCaptureSessionId('cs1'))
    act(() => result.current.load())
    expect(refetch).toHaveBeenCalledTimes(1)
  })

  it('builds raw queries, honouring selected sensors and late data', () => {
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    expect(lastArgs.current).toMatchObject({ mode: 'raw', sensorIds: undefined, includeLate: true, maxPoints: 5000, pageSize: 2000, enabled: false })
    act(() => result.current.add('s1'))
    act(() => result.current.setIncludeLate(false))
    act(() => result.current.setOrder('desc'))
    expect(lastArgs.current).toMatchObject({ mode: 'raw', sensorIds: ['s1'], includeLate: false, order: 'desc' })
  })

  it('builds aggregated queries when requested', () => {
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    act(() => result.current.setUseAggregated(true))
    expect(lastArgs.current).toMatchObject({ mode: 'aggregated', limit: 5000, hardCapLimit: 20000 })
    expect(lastArgs.current).not.toHaveProperty('includeLate')
  })

  it('caps displayMaxPoints and falls back for invalid values', () => {
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    act(() => result.current.setMaxPoints(50000))
    expect(result.current.displayMaxPoints).toBe(20000)
    act(() => result.current.setMaxPoints(-3))
    expect(result.current.displayMaxPoints).toBe(5000)
    act(() => result.current.setMaxPoints(Number.NaN))
    expect(result.current.displayMaxPoints).toBe(5000)
  })

  it('groups raw points per sensor, ordered by id, in physical or raw mode', () => {
    Object.assign(queryState, {
      points: [pt(3, 's1', '2026-01-01T00:00:03Z', 30, 300), pt(1, 's1', '2026-01-01T00:00:01Z', 10, 100), pt(2, 'ghost', '2026-01-01T00:00:02Z', 20, 200)],
    })
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    expect(result.current.hasData).toBe(true)
    expect(result.current.rawSeries).toEqual([
      { name: 'Thermo', x: ['2026-01-01T00:00:01Z', '2026-01-01T00:00:03Z'], y: [10, 30] },
      { name: 'ghost', x: ['2026-01-01T00:00:02Z'], y: [20] },
    ])
    act(() => result.current.setValueMode('raw'))
    expect(result.current.rawSeries[0].y).toEqual([100, 300])
    expect(result.current.effectiveSensorIds).toEqual(['s1', 'ghost'])
    expect(result.current.lastTimestamp).toBe('2026-01-01T00:00:03Z')
    expect(result.current.cursorBySensor).toEqual({
      s1: { timestamp: '2026-01-01T00:00:03Z', id: 3 },
      ghost: { timestamp: '2026-01-01T00:00:02Z', id: 2 },
    })
  })

  it('uses selected sensors as effective ids when present, and picks the highest id for equal timestamps', () => {
    Object.assign(queryState, {
      points: [pt(1, 's1', '2026-01-01T00:00:01Z'), pt(5, 's1', '2026-01-01T00:00:01Z'), pt(2, 's1', '2026-01-01T00:00:01Z')],
    })
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    act(() => result.current.add('s2'))
    expect(result.current.effectiveSensorIds).toEqual(['s2'])
    expect(result.current.cursorBySensor.s1).toEqual({ timestamp: '2026-01-01T00:00:01Z', id: 5 })
  })

  it('reports no last timestamp without points', () => {
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    expect(result.current.lastTimestamp).toBeNull()
    expect(result.current.cursorBySensor).toEqual({})
  })

  it('groups aggregated buckets per sensor and signal', () => {
    Object.assign(queryState, {
      mode: 'aggregated',
      buckets: [
        { sensor_id: 's1', signal: 'x', bucket: '2026-01-01T00:02:00Z', avg_physical: 2, min_physical: 1, max_physical: 3, avg_raw: 20, min_raw: 10, max_raw: 30 },
        { sensor_id: 's1', signal: 'x', bucket: '2026-01-01T00:01:00Z', avg_physical: 1, min_physical: 0, max_physical: 2, avg_raw: 10, min_raw: 0, max_raw: 20 },
        { sensor_id: 'ghost', bucket: '2026-01-01T00:01:00Z', avg_physical: 5, min_physical: 5, max_physical: 5, avg_raw: 50, min_raw: 50, max_raw: 50 },
        { bucket: '2026-01-01T00:01:00Z', avg_physical: 9, min_physical: 9, max_physical: 9, avg_raw: 90, min_raw: 90, max_raw: 90 },
      ],
    })
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    act(() => result.current.setUseAggregated(true))
    expect(result.current.hasData).toBe(true)
    const names = result.current.aggregatedSeries.map((s) => s.name)
    expect(names).toEqual(['Thermo / x', 'ghost', ''])
    expect(result.current.aggregatedSeries[0]).toMatchObject({
      x: ['2026-01-01T00:01:00Z', '2026-01-01T00:02:00Z'], avg: [1, 2], min: [0, 1], max: [2, 3],
    })
    act(() => result.current.setValueMode('raw'))
    expect(result.current.aggregatedSeries[0]).toMatchObject({ avg: [10, 20], min: [0, 10], max: [20, 30] })
    // raw-mode data accessors are empty in aggregated mode
    expect(result.current.rawSeries).toEqual([])
  })

  it('exposes loading, loaded count, errors and truncation from the query', () => {
    Object.assign(queryState, { isFetching: true, loadedCount: 12, error: new Error('boom'), wasTruncated: true })
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    expect(result.current.loading).toBe(true)
    expect(result.current.loadedCount).toBe(12)
    expect(result.current.loadError).toBe('boom')
    expect(result.current.wasTruncated).toBe(true)
  })

  it('has no truncation flag in aggregated mode', () => {
    Object.assign(queryState, { mode: 'aggregated', wasTruncated: true, error: { message: '' } })
    const { result } = renderHook(() => useTelemetryHistory(sensors))
    expect(result.current.wasTruncated).toBe(false)
    expect(result.current.loadError).toBeNull()
  })
})
