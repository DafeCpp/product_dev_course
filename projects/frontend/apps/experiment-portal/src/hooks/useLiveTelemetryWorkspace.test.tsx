import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { renderHook, render, act } from '@testing-library/react'
import useLiveTelemetryWorkspace from './useLiveTelemetryWorkspace'

let uuidCounter = 0
vi.mock('../utils/uuid', () => ({ generateUUID: () => `id-${++uuidCounter}` }))

describe('useLiveTelemetryWorkspace', () => {
  beforeEach(() => {
    uuidCounter = 0
    window.localStorage.clear()
  })
  afterEach(() => vi.unstubAllGlobals())

  it('starts empty and persists changes', () => {
    const { result } = renderHook(() => useLiveTelemetryWorkspace())
    expect(result.current.panelIds).toEqual([])
    act(() => result.current.addPanel())
    act(() => result.current.addPanel())
    expect(result.current.panelIds).toEqual(['id-1', 'id-2'])
    expect(JSON.parse(window.localStorage.getItem('telemetry_panel_ids')!)).toEqual(['id-1', 'id-2'])
  })

  it('restores persisted panel ids, dropping invalid entries', () => {
    window.localStorage.setItem('telemetry_panel_ids', JSON.stringify(['a', '', '  ', 3, 'b']))
    const { result } = renderHook(() => useLiveTelemetryWorkspace())
    expect(result.current.panelIds).toEqual(['a', 'b'])
  })

  it('ignores non-array and malformed persisted values', () => {
    window.localStorage.setItem('telemetry_panel_ids', '{"a":1}')
    expect(renderHook(() => useLiveTelemetryWorkspace()).result.current.panelIds).toEqual([])
    window.localStorage.setItem('telemetry_panel_ids', 'nope')
    expect(renderHook(() => useLiveTelemetryWorkspace()).result.current.panelIds).toEqual([])
  })

  it('removes panels', () => {
    window.localStorage.setItem('telemetry_panel_ids', JSON.stringify(['a', 'b', 'c']))
    const { result } = renderHook(() => useLiveTelemetryWorkspace())
    act(() => result.current.removePanel('b'))
    expect(result.current.panelIds).toEqual(['a', 'c'])
  })

  it('moves a panel to the target position and ignores invalid moves', () => {
    window.localStorage.setItem('telemetry_panel_ids', JSON.stringify(['a', 'b', 'c']))
    const { result } = renderHook(() => useLiveTelemetryWorkspace())
    act(() => result.current.movePanel('a', 'c'))
    expect(result.current.panelIds).toEqual(['b', 'c', 'a'])
    act(() => result.current.movePanel('a', 'a'))
    act(() => result.current.movePanel('zzz', 'b'))
    act(() => result.current.movePanel('b', 'zzz'))
    expect(result.current.panelIds).toEqual(['b', 'c', 'a'])
  })

  it('tracks panel sizes without re-setting identical ones', () => {
    const { result } = renderHook(() => useLiveTelemetryWorkspace())
    act(() => result.current.onSizeChange('a', { width: 1, height: 2 }))
    const first = result.current.panelSizes
    act(() => result.current.onSizeChange('a', { width: 1, height: 2 }))
    expect(result.current.panelSizes).toBe(first)
    act(() => result.current.onSizeChange('a', { width: 3, height: 2 }))
    expect(result.current.panelSizes.a).toEqual({ width: 3, height: 2 })
  })

  it('keeps only the latest 30 recent values per sensor', () => {
    const { result } = renderHook(() => useLiveTelemetryWorkspace())
    for (let i = 0; i < 35; i++) act(() => result.current.onRecordReceived('s1', i))
    act(() => result.current.onRecordReceived('s2', 1))
    expect(result.current.recentValuesBySensor.s1).toHaveLength(30)
    expect(result.current.recentValuesBySensor.s1[0]).toBe(5)
    expect(result.current.recentValuesBySensor.s1[29]).toBe(34)
    expect(result.current.recentValuesBySensor.s2).toEqual([1])
  })

  it('tracks drag state', () => {
    const { result } = renderHook(() => useLiveTelemetryWorkspace())
    act(() => result.current.setDraggingPanelId('a'))
    act(() => result.current.setDragOverPanelId('b'))
    expect(result.current.draggingPanelId).toBe('a')
    expect(result.current.dragOverPanelId).toBe('b')
  })

  it('observes the wrapper width through ResizeObserver and disconnects on unmount', () => {
    const observers: FakeRO[] = []
    class FakeRO {
      cb: () => void
      disconnect = vi.fn()
      observe = vi.fn()
      constructor(cb: () => void) { this.cb = cb; observers.push(this) }
    }
    vi.stubGlobal('ResizeObserver', FakeRO)
    let width = 420.6
    const seen: number[] = []
    function Harness() {
      const api = useLiveTelemetryWorkspace()
      seen.push(api.panelsWrapWidth)
      return (
        <div
          ref={(el) => {
            if (el) el.getBoundingClientRect = () => ({ width }) as DOMRect
            api.panelsWrapRef.current = el
          }}
          data-testid="wrap"
        />
      )
    }
    const { unmount } = render(<Harness />)
    expect(observers).toHaveLength(1)
    expect(observers[0].observe).toHaveBeenCalled()
    expect(seen[seen.length - 1]).toBe(421)
    width = -5
    act(() => observers[0].cb())
    expect(seen[seen.length - 1]).toBe(0)
    unmount()
    expect(observers[0].disconnect).toHaveBeenCalled()
  })

  it('skips observing when ResizeObserver is unavailable', () => {
    vi.stubGlobal('ResizeObserver', undefined)
    function Harness() {
      const api = useLiveTelemetryWorkspace()
      return <div ref={api.panelsWrapRef} />
    }
    expect(() => render(<Harness />)).not.toThrow()
  })
})
