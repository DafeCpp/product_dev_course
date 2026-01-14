export type ToastKind = 'debug-http-error' | 'text'
export type ToastVariant = 'error' | 'success' | 'info'

export type ToastEvent = {
  kind: ToastKind
  variant?: ToastVariant
  title: string
  message?: string
  /**
   * Optional structured payload for specialized toast renderers.
   * Keep this serializable-ish so it can be copied/logged if needed.
   */
  payload?: unknown
  /** Auto-dismiss duration. If omitted, provider will use a default. */
  durationMs?: number
}

type ToastHandler = (evt: ToastEvent) => void

const _handlers = new Set<ToastHandler>()

export function emitToast(evt: ToastEvent): void {
  for (const h of _handlers) {
    try {
      h(evt)
    } catch (e) {
      // Never allow UI notification plumbing to crash the app.
      console.error('toast handler failed', e)
    }
  }
}

export function subscribeToasts(handler: ToastHandler): () => void {
  _handlers.add(handler)
  return () => _handlers.delete(handler)
}

