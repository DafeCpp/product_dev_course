import { describe, expect, it } from 'vitest'

import { palette, themes } from '@lostpointer/web-tokens'
import theme from './theme'

describe('theme', () => {
  it('derives the MUI palette from the shared token contract', () => {
    expect(theme.palette.primary.main).toBe(themes.light.primary)
    expect(theme.palette.primary.dark).toBe(themes.light.primaryHover)
    expect(theme.palette.primary.light).toBe(palette.indigo[300])
    expect(theme.palette.error.main).toBe(themes.light.error)
    expect(theme.palette.warning.main).toBe(themes.light.warning)
    expect(theme.palette.success.main).toBe(themes.light.success)
    expect(theme.palette.info.main).toBe(themes.light.info)
  })
})
