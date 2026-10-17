import { describe, it, expect, vi, afterEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import DebugErrorToast from './DebugErrorToast'
import type { HttpDebugInfo } from '../../utils/httpDebug'

const info = (over: Record<string, unknown> = {}) => ({
  request: { method: 'POST', url: '/api/y' },
  response: { status: 502 },
  correlation: { trace_id: 'tr', request_id: 'rq' },
  ...over,
}) as unknown as HttpDebugInfo

afterEach(() => { vi.restoreAllMocks() })

describe('DebugErrorToast', () => {
  it('summarises the request, ids and toggles details', async () => {
    render(<DebugErrorToast info={info()} />)
    expect(screen.getByText('HTTP 502')).toBeInTheDocument()
    expect(screen.getByText('POST')).toBeInTheDocument()
    expect(screen.getByText('/api/y')).toBeInTheDocument()
    expect(screen.getByText('trace_id=tr request_id=rq')).toBeInTheDocument()
    expect(document.querySelector('pre')).toBeNull()
    await userEvent.click(screen.getByText('Details'))
    expect(document.querySelector('pre')).not.toBeNull()
    await userEvent.click(screen.getByText('Hide'))
    expect(document.querySelector('pre')).toBeNull()
  })

  it('reports a network error without ids or request line', () => {
    render(<DebugErrorToast info={info({ response: undefined, correlation: undefined, request: { method: '', url: '' } })} />)
    expect(screen.getByText('Network error')).toBeInTheDocument()
    expect(document.querySelector('.debug-toast-ids')).toBeNull()
    expect(document.querySelector('.debug-toast-req')).toBeNull()
  })

  it('copies through the clipboard API', async () => {
    const writeText = vi.fn().mockResolvedValue(undefined)
    Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true })
    render(<DebugErrorToast info={info()} />)
    await userEvent.click(screen.getByText('Copy'))
    expect(writeText).toHaveBeenCalledTimes(1)
    await waitFor(() => expect(screen.getByText('Copied')).toBeInTheDocument())
  })

  it('falls back to execCommand when the clipboard API fails', async () => {
    Object.defineProperty(navigator, 'clipboard', { value: { writeText: vi.fn().mockRejectedValue(new Error('x')) }, configurable: true })
    const exec = vi.fn().mockReturnValue(true)
    Object.defineProperty(document, 'execCommand', { value: exec, configurable: true })
    render(<DebugErrorToast info={info()} />)
    await userEvent.click(screen.getByText('Copy'))
    await waitFor(() => expect(exec).toHaveBeenCalledWith('copy'))
    expect(screen.getByText('Copied')).toBeInTheDocument()
    expect(document.querySelector('textarea')).toBeNull()
  })

  it('stays silent when both copy strategies fail', async () => {
    Object.defineProperty(navigator, 'clipboard', { value: { writeText: vi.fn().mockRejectedValue(new Error('x')) }, configurable: true })
    Object.defineProperty(document, 'execCommand', { value: vi.fn(() => { throw new Error('y') }), configurable: true })
    render(<DebugErrorToast info={info()} />)
    await userEvent.click(screen.getByText('Copy'))
    await waitFor(() => expect(screen.getByText('Copy')).toBeInTheDocument())
    expect(screen.queryByText('Copied')).toBeNull()
  })
})
