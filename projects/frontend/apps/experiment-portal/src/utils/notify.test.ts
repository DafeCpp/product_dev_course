import { describe, it, expect, vi, beforeEach } from 'vitest'

const emitToast = vi.fn()
vi.mock('./toastBus', () => ({ emitToast: (e: unknown) => emitToast(e) }))

describe('notify (outside test mode)', () => {
  beforeEach(() => {
    emitToast.mockClear()
    vi.resetModules()
    vi.doMock('./env', () => ({ IS_TEST: false }))
  })

  it('emits error, success and sticky toasts with their durations', async () => {
    const n = await import('./notify')
    n.notifyError('  плохо ')
    n.notifySuccess('хорошо')
    n.notifySuccess('ок', 'Готово')
    n.notifySuccessSticky('липко')
    expect(emitToast.mock.calls.map((c) => c[0])).toEqual([
      expect.objectContaining({ variant: 'error', title: 'Ошибка', message: 'плохо', durationMs: 6000 }),
      expect.objectContaining({ variant: 'success', title: 'Успех', message: 'хорошо', durationMs: 4000 }),
      expect.objectContaining({ title: 'Готово', message: 'ок' }),
      expect.objectContaining({ variant: 'success', message: 'липко', durationMs: 0 }),
    ])
  })

  it('ignores blank messages', async () => {
    const n = await import('./notify')
    n.notifyError('   ')
    n.notifySuccess('')
    n.notifySuccessSticky('  ')
    expect(emitToast).not.toHaveBeenCalled()
  })
})

describe('notify (test mode)', () => {
  it('is silent', async () => {
    emitToast.mockClear()
    vi.resetModules()
    vi.doMock('./env', () => ({ IS_TEST: true }))
    const n = await import('./notify')
    n.notifyError('x'); n.notifySuccess('x'); n.notifySuccessSticky('x')
    expect(emitToast).not.toHaveBeenCalled()
  })
})
