import { useCallback, useEffect, useRef, useState } from 'react'
import { createSSEParser } from '../sse'
import type { TelemetryStreamOpener, TelemetryStreamRecord } from '../types/telemetry'
import { computeBackoffDelayMs, type TelemetryBackoffOptions } from './backoff'

export type TelemetryStreamStatus =
  | 'idle'
  | 'connecting'
  | 'streaming'
  | 'reconnecting'
  | 'error'
  | 'stopped'

export interface UseTelemetryStreamOptions {
  open: TelemetryStreamOpener
  bufferSize?: number
  idleTimeoutSeconds?: number
  initialSinceTs?: string
  initialSinceId?: number
  autoReconnect?: boolean
  backoff?: TelemetryBackoffOptions
  onRecord?: (record: TelemetryStreamRecord) => void
  onError?: (error: Error, ctx: { willRetry: boolean; attempt: number }) => void
}

export interface TelemetryStreamCursor {
  sinceTs?: string
  sinceId?: number
}

export interface UseTelemetryStreamResult {
  status: TelemetryStreamStatus
  points: TelemetryStreamRecord[]
  lastRecord: TelemetryStreamRecord | null
  error: Error | null
  reconnectAttempt: number
  cursor: TelemetryStreamCursor
  start: (override?: TelemetryStreamCursor) => void
  stop: () => void
  clear: () => void
}

const DEFAULT_BUFFER_SIZE = 5000
const DEFAULT_IDLE_TIMEOUT_SECONDS = 30

export function useTelemetryStream(
  sensorId: string,
  opts: UseTelemetryStreamOptions,
): UseTelemetryStreamResult {
  const { initialSinceTs, initialSinceId } = opts

  const [status, setStatus] = useState<TelemetryStreamStatus>('idle')
  const [points, setPoints] = useState<TelemetryStreamRecord[]>([])
  const [lastRecord, setLastRecord] = useState<TelemetryStreamRecord | null>(null)
  const [error, setError] = useState<Error | null>(null)
  const [reconnectAttempt, setReconnectAttempt] = useState(0)
  const [cursor, setCursor] = useState<TelemetryStreamCursor>({ sinceTs: initialSinceTs, sinceId: initialSinceId })

  // Refs mirror the above so the async read loop always sees fresh values,
  // without re-subscribing effects on every state update.
  const cursorRef = useRef<TelemetryStreamCursor>({ sinceTs: initialSinceTs, sinceId: initialSinceId })
  const intentionalStopRef = useRef(false)
  const attemptRef = useRef(0)
  const abortControllerRef = useRef<AbortController | null>(null)
  const reconnectTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null)
  const generationRef = useRef(0)

  // Keep the latest injected callbacks/options in refs so `connect` (defined
  // once via useCallback deps below) doesn't go stale between renders.
  const optsRef = useRef(opts)
  optsRef.current = opts

  const clearReconnectTimer = useCallback(() => {
    if (reconnectTimerRef.current !== null) {
      clearTimeout(reconnectTimerRef.current)
      reconnectTimerRef.current = null
    }
  }, [])

  const connect = useCallback(
    (generation: number) => {
      const controller = new AbortController()
      abortControllerRef.current = controller
      setStatus(attemptRef.current > 0 ? 'reconnecting' : 'connecting')

      const currentOpts = optsRef.current
      const maxBufferSize = currentOpts.bufferSize ?? DEFAULT_BUFFER_SIZE
      const currentIdleTimeoutSeconds = currentOpts.idleTimeoutSeconds ?? DEFAULT_IDLE_TIMEOUT_SECONDS

      const finishWithReconnectDecision = (err: Error | null) => {
        if (generation !== generationRef.current) return
        if (intentionalStopRef.current) {
          setStatus('stopped')
          return
        }
        const shouldAutoReconnect = currentOpts.autoReconnect ?? true
        const maxAttempts = currentOpts.backoff?.maxAttempts ?? Infinity
        const nextAttempt = attemptRef.current + 1

        if (!shouldAutoReconnect || nextAttempt > maxAttempts) {
          const finalError = err ?? new Error('Telemetry stream ended')
          setError(finalError)
          setStatus('error')
          currentOpts.onError?.(finalError, { willRetry: false, attempt: attemptRef.current })
          return
        }

        attemptRef.current = nextAttempt
        setReconnectAttempt(nextAttempt)
        setStatus('reconnecting')
        if (err) {
          currentOpts.onError?.(err, { willRetry: true, attempt: nextAttempt })
        }

        const delay = computeBackoffDelayMs(nextAttempt, currentOpts.backoff)
        reconnectTimerRef.current = setTimeout(() => {
          reconnectTimerRef.current = null
          connect(generation)
        }, delay)
      }

      currentOpts
        .open({
          sensorId,
          sinceTs: cursorRef.current.sinceTs,
          sinceId: cursorRef.current.sinceId,
          idleTimeoutSeconds: currentIdleTimeoutSeconds,
          signal: controller.signal,
        })
        .then(async ({ response }) => {
          if (generation !== generationRef.current) return
          if (!response.body) {
            throw new Error('Telemetry stream response has no body')
          }

          attemptRef.current = 0
          setReconnectAttempt(0)
          setError(null)
          setStatus('streaming')

          const reader = response.body.getReader()
          const decoder = new TextDecoder()
          const parser = createSSEParser((evt) => {
            if (evt.event === 'error') {
              throw new Error(evt.data || 'Telemetry stream reported an error')
            }
            if (evt.event !== 'telemetry' && evt.event !== 'message') return
            let record: TelemetryStreamRecord
            try {
              record = JSON.parse(evt.data) as TelemetryStreamRecord
            } catch {
              return
            }
            cursorRef.current = { sinceTs: record.timestamp, sinceId: record.id }
            setCursor(cursorRef.current)
            setPoints((prev) => {
              const updated = [...prev, record]
              return updated.length > maxBufferSize
                ? updated.slice(updated.length - maxBufferSize)
                : updated
            })
            setLastRecord(record)
            currentOpts.onRecord?.(record)
          })

          while (true) {
            const { value, done } = await reader.read()
            if (generation !== generationRef.current) return
            if (done) break
            if (value) parser.feed(decoder.decode(value, { stream: true }))
          }

          finishWithReconnectDecision(null)
        })
        .catch((err: unknown) => {
          if (generation !== generationRef.current) return
          if (err instanceof Error && err.name === 'AbortError') {
            // Intentional abort (stop()/unmount) — handled by stop(), not a reconnect trigger.
            return
          }
          finishWithReconnectDecision(err instanceof Error ? err : new Error(String(err)))
        })
    },
    [sensorId],
  )

  const start = useCallback(
    (override?: TelemetryStreamCursor) => {
      clearReconnectTimer()
      if (abortControllerRef.current) {
        abortControllerRef.current.abort()
        abortControllerRef.current = null
      }
      generationRef.current += 1
      intentionalStopRef.current = false
      attemptRef.current = 0
      setReconnectAttempt(0)
      setError(null)
      setPoints([])
      setLastRecord(null)

      const nextCursor: TelemetryStreamCursor = override ?? { sinceTs: initialSinceTs, sinceId: initialSinceId }
      cursorRef.current = nextCursor
      setCursor(nextCursor)

      connect(generationRef.current)
    },
    [clearReconnectTimer, connect, initialSinceTs, initialSinceId],
  )

  const stop = useCallback(() => {
    intentionalStopRef.current = true
    clearReconnectTimer()
    generationRef.current += 1
    if (abortControllerRef.current) {
      abortControllerRef.current.abort()
      abortControllerRef.current = null
    }
    setStatus('stopped')
  }, [clearReconnectTimer])

  const clear = useCallback(() => {
    setPoints([])
    setLastRecord(null)
    const resetCursor: TelemetryStreamCursor = { sinceTs: initialSinceTs, sinceId: initialSinceId }
    cursorRef.current = resetCursor
    setCursor(resetCursor)
  }, [initialSinceTs, initialSinceId])

  useEffect(() => {
    return () => {
      intentionalStopRef.current = true
      clearReconnectTimer()
      generationRef.current += 1
      if (abortControllerRef.current) {
        abortControllerRef.current.abort()
        abortControllerRef.current = null
      }
    }
  }, [clearReconnectTimer])

  return { status, points, lastRecord, error, reconnectAttempt, cursor, start, stop, clear }
}
