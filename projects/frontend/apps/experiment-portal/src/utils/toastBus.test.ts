import { describe, it, expect, vi } from 'vitest'
import { emitToast, subscribeToasts } from './toastBus'

describe('toastBus', () => {
  it('delivers events to subscribers until unsubscribed', () => {
    const handler = vi.fn()
    const off = subscribeToasts(handler)
    emitToast({ kind: 'text', title: 'a' })
    expect(handler).toHaveBeenCalledWith({ kind: 'text', title: 'a' })
    off()
    emitToast({ kind: 'text', title: 'b' })
    expect(handler).toHaveBeenCalledTimes(1)
  })

  it('isolates a throwing handler from the others', () => {
    const spy = vi.spyOn(console, 'error').mockImplementation(() => {})
    const good = vi.fn()
    const offBad = subscribeToasts(() => { throw new Error('boom') })
    const offGood = subscribeToasts(good)
    emitToast({ kind: 'text', title: 'x' })
    expect(good).toHaveBeenCalledTimes(1)
    expect(spy).toHaveBeenCalled()
    offBad(); offGood(); spy.mockRestore()
  })
})
