import { describe, it, expect } from 'vitest'
import { formatNumber, formatBytes, formatHz } from './number'

describe('formatNumber', () => {
  it('formats number with ru-RU locale (comma as decimal separator)', () => {
    const result = formatNumber(1234.56)
    // Check format without depending on exact space type
    expect(result).toMatch(/^1\s*234,56$/)
    expect(formatNumber(1000)).toMatch(/^1\s*000,00$/)
  })

  it('respects fractionDigits parameter', () => {
    expect(formatNumber(1234.5678, 1)).toMatch(/234,6/)
    expect(formatNumber(1234.5678, 3)).toMatch(/234,568/)
    expect(formatNumber(1234, 0)).toMatch(/^1\s*234$/)
  })

  it('returns "—" for non-finite values', () => {
    expect(formatNumber(Infinity)).toBe('—')
    expect(formatNumber(-Infinity)).toBe('—')
    expect(formatNumber(NaN)).toBe('—')
  })
})

describe('formatBytes', () => {
  it('formats bytes to B, KB, MB, GB', () => {
    expect(formatBytes(0)).toBe('0 B')
    expect(formatBytes(512)).toBe('512 B')
    expect(formatBytes(1024)).toBe('1.0 KB')
    expect(formatBytes(1024 * 1024)).toBe('1.0 MB')
    expect(formatBytes(1024 * 1024 * 1024)).toBe('1.0 GB')
    expect(formatBytes(1024 * 1024 * 1024 * 1024)).toBe('1.0 TB')
  })

  it('shows decimal for KB and above', () => {
    expect(formatBytes(1536)).toBe('1.5 KB') // 1.5 * 1024
    expect(formatBytes(5 * 1024 * 1024 + 512 * 1024)).toBe('5.5 MB')
  })

  it('returns "—" for negative or non-finite values', () => {
    expect(formatBytes(-1024)).toBe('—')
    expect(formatBytes(Infinity)).toBe('—')
    expect(formatBytes(NaN)).toBe('—')
  })
})

describe('formatHz', () => {
  it('formats Hz, kHz, MHz', () => {
    expect(formatHz(500)).toBe('500.0 Hz')
    expect(formatHz(1000)).toBe('1.0 kHz')
    expect(formatHz(500_000)).toBe('500.0 kHz')
    expect(formatHz(1_000_000)).toBe('1.0 MHz')
    expect(formatHz(2_500_000)).toBe('2.5 MHz')
  })

  it('returns "—" for negative or non-finite values', () => {
    expect(formatHz(-500)).toBe('—')
    expect(formatHz(Infinity)).toBe('—')
    expect(formatHz(NaN)).toBe('—')
  })
})
