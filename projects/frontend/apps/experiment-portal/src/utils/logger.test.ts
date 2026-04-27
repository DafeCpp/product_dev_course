import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { logger } from './logger'

describe('logger in DEV mode', () => {
  beforeEach(() => {
    vi.stubEnv('DEV', true)
    vi.spyOn(console, 'debug').mockImplementation(() => {})
    vi.spyOn(console, 'info').mockImplementation(() => {})
    vi.spyOn(console, 'warn').mockImplementation(() => {})
    vi.spyOn(console, 'error').mockImplementation(() => {})
  })

  afterEach(() => {
    vi.unstubAllEnvs()
    vi.restoreAllMocks()
  })

  it('debug calls console.debug with formatted message', () => {
    logger.debug('test message', { data: 123 })
    expect(console.debug).toHaveBeenCalledWith('[DEBUG] test message', { data: 123 })
  })

  it('info calls console.info', () => {
    logger.info('info message')
    expect(console.info).toHaveBeenCalledWith('[INFO] info message', undefined)
  })

  it('warn calls console.warn', () => {
    logger.warn('warn message')
    expect(console.warn).toHaveBeenCalledWith('[WARN] warn message', undefined)
  })

  it('error calls console.error', () => {
    const err = new Error('test error')
    logger.error('error occurred', err)
    expect(console.error).toHaveBeenCalledWith('[ERROR] error occurred', err)
  })
})

describe('logger in non-DEV mode', () => {
  beforeEach(() => {
    vi.stubEnv('DEV', false)
    vi.spyOn(console, 'debug').mockImplementation(() => {})
    vi.spyOn(console, 'info').mockImplementation(() => {})
    vi.spyOn(console, 'warn').mockImplementation(() => {})
    vi.spyOn(console, 'error').mockImplementation(() => {})
  })

  afterEach(() => {
    vi.unstubAllEnvs()
    vi.restoreAllMocks()
  })

  it('suppresses debug logs', () => {
    logger.debug('test')
    expect(console.debug).not.toHaveBeenCalled()
  })

  it('suppresses info logs', () => {
    logger.info('test')
    expect(console.info).not.toHaveBeenCalled()
  })

  it('suppresses warn logs', () => {
    logger.warn('test')
    expect(console.warn).not.toHaveBeenCalled()
  })

  it('suppresses error logs', () => {
    logger.error('test')
    expect(console.error).not.toHaveBeenCalled()
  })
})
