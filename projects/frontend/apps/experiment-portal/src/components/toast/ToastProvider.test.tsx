import { describe, it, expect, vi, afterEach } from 'vitest'
import { act, render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import ToastProvider from './ToastProvider'
import { emitToast } from '../../utils/toastBus'
import type { HttpDebugInfo } from '../../utils/httpDebug'

const debugInfo: HttpDebugInfo = {
  request: { method: 'GET', url: '/api/x' },
  response: { status: 500 },
  correlation: { trace_id: 't1', request_id: 'r1' },
} as HttpDebugInfo

afterEach(() => { vi.useRealTimers() })

describe('ToastProvider', () => {
  it('renders children and a text toast, and closes it by button', async () => {
    render(<ToastProvider><p>app</p></ToastProvider>)
    expect(screen.getByText('app')).toBeInTheDocument()
    act(() => emitToast({ kind: 'text', variant: 'success', title: 'Готово', message: 'сохранено', durationMs: 0 }))
    expect(screen.getByText('сохранено')).toBeInTheDocument()
    expect(screen.getByText('Готово').closest('.toast-card')).toHaveClass('toast-success')
    await userEvent.click(screen.getByLabelText('Close'))
    expect(screen.queryByText('Готово')).toBeNull()
  })

  it('auto-dismisses after the duration and keeps only the newest four', () => {
    vi.useFakeTimers()
    render(<ToastProvider><span /></ToastProvider>)
    act(() => emitToast({ kind: 'text', title: 'short', durationMs: 1000 }))
    expect(screen.getByText('short')).toBeInTheDocument()
    act(() => { vi.advanceTimersByTime(1001) })
    expect(screen.queryByText('short')).toBeNull()

    act(() => { for (let i = 0; i < 6; i++) emitToast({ kind: 'text', title: `t${i}`, durationMs: 0 }) })
    expect(screen.queryByText('t0')).toBeNull()
    expect(screen.queryByText('t1')).toBeNull()
    expect(screen.getByText('t5')).toBeInTheDocument()
    expect(screen.getAllByLabelText('Close')).toHaveLength(4)
  })

  it('uses the default duration when none is given', () => {
    vi.useFakeTimers()
    render(<ToastProvider><span /></ToastProvider>)
    act(() => emitToast({ kind: 'text', title: 'dflt' }))
    act(() => { vi.advanceTimersByTime(7999) })
    expect(screen.getByText('dflt')).toBeInTheDocument()
    act(() => { vi.advanceTimersByTime(2) })
    expect(screen.queryByText('dflt')).toBeNull()
  })

  it('pins a toast on hover so it no longer expires', () => {
    vi.useFakeTimers()
    render(<ToastProvider><span /></ToastProvider>)
    act(() => emitToast({ kind: 'text', title: 'pinme', message: 'm', durationMs: 1000 }))
    const card = screen.getByText('pinme').closest('.toast-card') as HTMLElement
    act(() => { card.dispatchEvent(new MouseEvent('mouseover', { bubbles: true })) })
    // React's onMouseEnter is driven by mouseover on the parent chain
    act(() => { vi.advanceTimersByTime(5000) })
    expect(screen.getByText('pinme')).toBeInTheDocument()
  })

  it('renders a debug http toast with an error variant by default', () => {
    render(<ToastProvider><span /></ToastProvider>)
    act(() => emitToast({ kind: 'debug-http-error', title: 'HTTP', payload: debugInfo, durationMs: 0 }))
    expect(screen.getByText('HTTP').closest('.toast-card')).toHaveClass('toast-error')
    expect(screen.getByText('HTTP 500')).toBeInTheDocument()
  })

  it('unsubscribes on unmount', () => {
    const { unmount } = render(<ToastProvider><span /></ToastProvider>)
    unmount()
    expect(() => act(() => emitToast({ kind: 'text', title: 'late' }))).not.toThrow()
  })
})
