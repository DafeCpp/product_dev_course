import { emitToast } from './toastBus'
import { IS_TEST } from './env'

export function notifyError(message: string): void {
    if (IS_TEST) return
    const msg = (message || '').trim()
    if (!msg) return

    emitToast({
        kind: 'text',
        variant: 'error',
        title: 'Ошибка',
        message: msg,
        durationMs: 6000,
    })
}

export function notifySuccess(message: string, title = 'Успех'): void {
    if (IS_TEST) return
    const msg = (message || '').trim()
    if (!msg) return

    emitToast({
        kind: 'text',
        variant: 'success',
        title,
        message: msg,
        durationMs: 4000,
    })
}

export function notifySuccessSticky(message: string, title = 'Успех'): void {
    if (IS_TEST) return
    const msg = (message || '').trim()
    if (!msg) return

    emitToast({
        kind: 'text',
        variant: 'success',
        title,
        message: msg,
        durationMs: 0,
    })
}
