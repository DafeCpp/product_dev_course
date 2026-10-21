import { describe, it, expect, vi } from 'vitest'
import { createTelemetryRecordsStore } from './telemetryRecordsStore'
import type { TelemetryStreamRecord } from '../../types'

const points = (n: number) => Array.from({ length: n }, (_, i) => ({ id: i }) as unknown as TelemetryStreamRecord)

describe('createTelemetryRecordsStore', () => {
  it('publishes a new snapshot per change and skips no-op writes', () => {
    const store = createTelemetryRecordsStore()
    const listener = vi.fn()
    const unsubscribe = store.subscribe(listener)
    const first = store.getSnapshot()

    const a = points(2)
    store.set('s1', a)
    expect(listener).toHaveBeenCalledTimes(1)
    expect(store.getSnapshot()).not.toBe(first)
    expect(store.getSnapshot().s1).toBe(a)

    const snapshot = store.getSnapshot()
    store.set('s1', a)
    expect(listener).toHaveBeenCalledTimes(1)
    expect(store.getSnapshot()).toBe(snapshot)

    unsubscribe()
    store.set('s2', points(1))
    expect(listener).toHaveBeenCalledTimes(1)
  })

  it('retains only the given sensors and clears everything', () => {
    const store = createTelemetryRecordsStore()
    store.set('s1', points(1))
    store.set('s2', points(1))
    const listener = vi.fn()
    store.subscribe(listener)

    store.retain(['s1', 's2', 's3'])
    expect(listener).not.toHaveBeenCalled()

    store.retain(['s2'])
    expect(Object.keys(store.getSnapshot())).toEqual(['s2'])
    expect(listener).toHaveBeenCalledTimes(1)

    store.clear()
    expect(store.getSnapshot()).toEqual({})
    store.clear()
    expect(listener).toHaveBeenCalledTimes(2)
  })
})
