import { ReactNode, useEffect, useMemo, useRef, useState } from 'react'
import type { ToastEvent } from '../../utils/toastBus'
import { subscribeToasts } from '../../utils/toastBus'
import type { HttpDebugInfo } from '../../utils/httpDebug'
import DebugErrorToast from './DebugErrorToast'
import './ToastViewport.scss'

type ToastItem = {
  id: string
  createdAt: number
  event: ToastEvent
}

const DEFAULT_DURATION_MS = 8000
const MAX_TOASTS = 4

function _now() {
  return Date.now()
}

function _id() {
  // Avoid depending on crypto for older environments; good enough for UI keys.
  return `${_now()}_${Math.random().toString(16).slice(2)}`
}

export default function ToastProvider({ children }: { children: ReactNode }) {
  const [toasts, setToasts] = useState<ToastItem[]>([])
  const timersRef = useRef(new Map<string, number>())

  const remove = (id: string) => {
    const t = timersRef.current.get(id)
    if (t) window.clearTimeout(t)
    timersRef.current.delete(id)
    setToasts((prev) => prev.filter((x) => x.id !== id))
  }

  const pin = (id: string) => {
    const t = timersRef.current.get(id)
    if (t) window.clearTimeout(t)
    timersRef.current.delete(id)
  }

  useEffect(() => {
    return subscribeToasts((evt) => {
      const item: ToastItem = { id: _id(), createdAt: _now(), event: evt }
      setToasts((prev) => [item, ...prev].slice(0, MAX_TOASTS))

      const duration = typeof evt.durationMs === 'number' ? evt.durationMs : DEFAULT_DURATION_MS
      if (duration > 0) {
        const timer = window.setTimeout(() => remove(item.id), duration)
        timersRef.current.set(item.id, timer)
      }
    })
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  useEffect(() => {
    // Cleanup on unmount
    return () => {
      for (const t of timersRef.current.values()) window.clearTimeout(t)
      timersRef.current.clear()
    }
  }, [])

  const rendered = useMemo(() => {
    return toasts.map((t) => {
      const msg = t.event.message || ''
      const variant = t.event.variant || (t.event.kind === 'debug-http-error' ? 'error' : undefined)
      const toastClass = variant ? `toast-card toast-${variant}` : 'toast-card'
      return (
        <div
          key={t.id}
          className={toastClass}
          onPointerDown={() => pin(t.id)}
          onMouseEnter={() => pin(t.id)}
          onFocusCapture={() => pin(t.id)}
        >
          <div className="toast-card-header">
            <div className="toast-title">{t.event.title}</div>
            <button
              type="button"
              className="toast-close"
              aria-label="Close"
              onClick={() => remove(t.id)}
            >
              ×
            </button>
          </div>
          <div className="toast-body">
            {t.event.kind === 'debug-http-error' && t.event.payload ? (
              <DebugErrorToast info={t.event.payload as HttpDebugInfo} />
            ) : msg ? (
              <div className="toast-message">{msg}</div>
            ) : null}
          </div>
        </div>
      )
    })
  }, [toasts])

  return (
    <>
      {children}
      <div className="toast-viewport" aria-live="polite" aria-relevant="additions">
        {rendered}
      </div>
    </>
  )
}

