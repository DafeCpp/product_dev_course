import type { TelemetryStreamRecord } from '../../types'

export type RecordsBySensor = Readonly<Record<string, TelemetryStreamRecord[]>>

/**
 * Holds the live records of one telemetry panel outside React state, so a
 * stream update re-renders only the chart that subscribes to it (via
 * useSyncExternalStore) and not the panel's header and settings.
 */
export interface TelemetryRecordsStore {
  getSnapshot: () => RecordsBySensor
  subscribe: (listener: () => void) => () => void
  set: (sensorId: string, points: TelemetryStreamRecord[]) => void
  clear: () => void
  /** Drops the records of sensors that are no longer in the panel. */
  retain: (sensorIds: readonly string[]) => void
}

export function createTelemetryRecordsStore(): TelemetryRecordsStore {
  let snapshot: RecordsBySensor = {}
  const listeners = new Set<() => void>()

  const replace = (next: RecordsBySensor) => {
    snapshot = next
    listeners.forEach((listener) => listener())
  }

  return {
    getSnapshot: () => snapshot,
    subscribe: (listener) => {
      listeners.add(listener)
      return () => {
        listeners.delete(listener)
      }
    },
    set: (sensorId, points) => {
      if (snapshot[sensorId] === points) return
      replace({ ...snapshot, [sensorId]: points })
    },
    clear: () => {
      if (Object.keys(snapshot).length === 0) return
      replace({})
    },
    retain: (sensorIds) => {
      const keep = new Set(sensorIds)
      const ids = Object.keys(snapshot)
      if (ids.every((id) => keep.has(id))) return
      replace(Object.fromEntries(ids.filter((id) => keep.has(id)).map((id) => [id, snapshot[id]])))
    },
  }
}
