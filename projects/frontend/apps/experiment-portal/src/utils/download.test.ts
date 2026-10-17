import { describe, it, expect, vi } from 'vitest'
import { downloadBlob } from './download'

describe('downloadBlob', () => {
  it('clicks a temporary link and releases the object url', () => {
    const create = vi.fn(() => 'blob:abc')
    const revoke = vi.fn()
    Object.assign(URL, { createObjectURL: create, revokeObjectURL: revoke })
    const click = vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(function (this: HTMLAnchorElement) {
      expect(this.download).toBe('data.csv')
      expect(this.href).toContain('blob:abc')
    })
    downloadBlob('a,b', 'data.csv', 'text/csv')
    expect(create).toHaveBeenCalledTimes(1)
    expect(click).toHaveBeenCalledTimes(1)
    expect(revoke).toHaveBeenCalledWith('blob:abc')
    expect(document.querySelector('a[download]')).toBeNull()
    click.mockRestore()
  })
})
