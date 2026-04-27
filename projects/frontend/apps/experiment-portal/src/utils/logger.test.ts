import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { logger } from './logger'

describe('logger', () => {
  beforeEach(() => {
    vi.spyOn(console, 'debug').mockImplementation(() => {})
    vi.spyOn(console, 'info').mockImplementation(() => {})
    vi.spyOn(console, 'warn').mockImplementation(() => {})
    vi.spyOn(console, 'error').mockImplementation(() => {})
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('logger methods exist', () => {
    expect(typeof logger.debug).toBe('function')
    expect(typeof logger.info).toBe('function')
    expect(typeof logger.warn).toBe('function')
    expect(typeof logger.error).toBe('function')
  })

  it('debug logs only in DEV mode', () => {
    const isDev = import.meta.env.DEV
    logger.debug('test message', { data: 123 })

    if (isDev) {
      expect(console.debug).toHaveBeenCalledWith('[DEBUG] test message', { data: 123 })
    } else {
      expect(console.debug).not.toHaveBeenCalled()
    }
  })

  it('info logs only in DEV mode', () => {
    const isDev = import.meta.env.DEV
    logger.info('info message')

    if (isDev) {
      expect(console.info).toHaveBeenCalledWith('[INFO] info message', undefined)
    } else {
      expect(console.info).not.toHaveBeenCalled()
    }
  })

  it('warn logs only in DEV mode', () => {
    const isDev = import.meta.env.DEV
    logger.warn('warn message')

    if (isDev) {
      expect(console.warn).toHaveBeenCalledWith('[WARN] warn message', undefined)
    } else {
      expect(console.warn).not.toHaveBeenCalled()
    }
  })

  it('error logs only in DEV mode', () => {
    const isDev = import.meta.env.DEV
    const err = new Error('test error')
    logger.error('error occurred', err)

    if (isDev) {
      expect(console.error).toHaveBeenCalledWith('[ERROR] error occurred', err)
    } else {
      expect(console.error).not.toHaveBeenCalled()
    }
  })
})
