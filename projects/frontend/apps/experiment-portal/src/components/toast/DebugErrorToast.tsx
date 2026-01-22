import { useMemo, useState } from 'react'
import type { HttpDebugInfo } from '../../utils/httpDebug'
import { formatHttpDebugText } from '../../utils/httpDebug'
import './DebugErrorToast.scss'

export default function DebugErrorToast({ info }: { info: HttpDebugInfo }) {
  const [open, setOpen] = useState(false)
  const [copied, setCopied] = useState(false)

  const summary = useMemo(() => {
    const status = info.response?.status
    const statusPart = typeof status === 'number' ? `HTTP ${status}` : 'Network error'
    const method = info.request.method || ''
    const url = info.request.url || ''
    const ids: string[] = []
    if (info.correlation?.trace_id) ids.push(`trace_id=${info.correlation.trace_id}`)
    if (info.correlation?.request_id) ids.push(`request_id=${info.correlation.request_id}`)
    return { statusPart, method, url, ids }
  }, [info])

  const copyText = useMemo(() => formatHttpDebugText(info), [info])

  const onCopy = async () => {
    try {
      await navigator.clipboard.writeText(copyText)
      setCopied(true)
      window.setTimeout(() => setCopied(false), 1200)
    } catch {
      // Fallback: best-effort.
      try {
        const ta = document.createElement('textarea')
        ta.value = copyText
        ta.style.position = 'fixed'
        ta.style.left = '-9999px'
        document.body.appendChild(ta)
        ta.select()
        document.execCommand('copy')
        document.body.removeChild(ta)
        setCopied(true)
        window.setTimeout(() => setCopied(false), 1200)
      } catch {
        // ignore
      }
    }
  }

  return (
    <div className="debug-toast">
      <div className="debug-toast-top">
        <div className="debug-toast-title">
          <span className="debug-toast-status">{summary.statusPart}</span>
          {summary.method || summary.url ? (
            <span className="debug-toast-req">
              <span className="toast-mono">{summary.method}</span>{' '}
              <span className="toast-mono">{summary.url}</span>
            </span>
          ) : null}
        </div>
        <div className="debug-toast-actions">
          <button type="button" className="btn btn-secondary btn-sm" onClick={onCopy}>
            {copied ? 'Copied' : 'Copy'}
          </button>
          <button
            type="button"
            className="btn btn-secondary btn-sm"
            onClick={() => setOpen((v) => !v)}
          >
            {open ? 'Hide' : 'Details'}
          </button>
        </div>
      </div>

      {summary.ids.length ? (
        <div className="debug-toast-ids toast-mono">{summary.ids.join(' ')}</div>
      ) : null}

      {open ? (
        <pre className="debug-toast-details toast-mono">{copyText}</pre>
      ) : null}
    </div>
  )
}

