import { describe, it, expect, vi, afterEach } from 'vitest'
import { HttpError } from '@lostpointer/web-http'
import { getActiveProjectId, setActiveProjectId, clearWorkspaceStorage } from './activeProject'
import { getCookie, getCsrfToken } from './csrf'
import { getApiErrorMessage, getErrorStatus, UserFacingError } from './apiError'
import { notifyError, notifySuccess, notifySuccessSticky } from './notify'

const emitToast = vi.fn()
vi.mock('./toastBus', () => ({ emitToast: (e: unknown) => emitToast(e) }))

afterEach(() => {
    vi.restoreAllMocks()
    vi.unstubAllGlobals()
    window.localStorage.clear()
})

describe('activeProject storage failures', () => {
    it('set/remove and survive storage exceptions', () => {
        setActiveProjectId('p1')
        expect(getActiveProjectId()).toBe('p1')
        setActiveProjectId('')
        expect(getActiveProjectId()).toBeNull()

        const boom = () => {
            throw new Error('disabled')
        }
        vi.spyOn(Storage.prototype, 'getItem').mockImplementation(boom)
        vi.spyOn(Storage.prototype, 'setItem').mockImplementation(boom)
        vi.spyOn(Storage.prototype, 'removeItem').mockImplementation(boom)
        expect(getActiveProjectId()).toBeNull()
        expect(() => setActiveProjectId('p2')).not.toThrow()
        expect(() => clearWorkspaceStorage()).not.toThrow()
    })
})

describe('csrf cookies', () => {
    it('returns undefined for empty cookie string', () => {
        vi.spyOn(document, 'cookie', 'get').mockReturnValue('')
        expect(getCsrfToken()).toBeUndefined()
    })

    it('skips blank and malformed parts, decodes values', () => {
        vi.spyOn(document, 'cookie', 'get').mockReturnValue('other=1; ; bare; csrf_token=a%20b')
        expect(getCsrfToken()).toBe('a b')
        expect(getCookie('missing')).toBeUndefined()
    })

    it('returns undefined without a document', () => {
        vi.stubGlobal('document', undefined)
        expect(getCookie('csrf_token')).toBeUndefined()
    })
})

describe('apiError leftovers', () => {
    it('keeps text whose code-point length fits although UTF-16 length exceeds the limit', () => {
        const text = '😀'.repeat(200)
        expect(text.length).toBe(400)
        expect(getApiErrorMessage({ response: { status: 400, data: { message: text } } }, 'fb')).toBe(text)
    })

    it('falls back when a usable HttpError message is blank after cleaning', () => {
        expect(getApiErrorMessage(new HttpError('api', 400, 'bad', '   '), 'fb')).toBe('fb')
    })

    it('falls back when a UserFacingError message is blank', () => {
        expect(getApiErrorMessage(new UserFacingError('  '), 'fb')).toBe('fb')
    })

    it('getErrorStatus is undefined for non-api HttpError kinds', () => {
        expect(getErrorStatus(new HttpError('network', 0, 'net', 'offline'))).toBeUndefined()
        expect(getErrorStatus(new HttpError('api', 409, 'conflict', 'c'))).toBe(409)
    })
})

describe('notify in test mode', () => {
    it('emits nothing while IS_TEST is true', () => {
        notifyError('x')
        notifySuccess('y')
        notifySuccessSticky('z')
        expect(emitToast).not.toHaveBeenCalled()
    })
})

describe('notify outside test mode with missing messages', () => {
    it('ignores undefined messages for error and sticky toasts', async () => {
        vi.resetModules()
        vi.doMock('./env', () => ({ IS_TEST: false }))
        const n = await import('./notify')
        n.notifyError(undefined as unknown as string)
        n.notifySuccessSticky(undefined as unknown as string)
        expect(emitToast).not.toHaveBeenCalled()
        vi.doUnmock('./env')
    })
})
