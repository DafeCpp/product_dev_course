import { createTheme } from '@mui/material/styles'
import { palette, themes } from '@lostpointer/web-tokens'

const colors = themes.light

const theme = createTheme({
  palette: {
    primary: {
      main: colors.primary,
      dark: colors.primaryHover,
      light: palette.indigo[300],
      contrastText: colors.onPrimary,
    },
    error: {
      main: colors.error,
      contrastText: colors.onPrimary,
    },
    warning: {
      main: colors.warning,
      contrastText: colors.onPrimary,
    },
    success: {
      main: colors.success,
      contrastText: colors.onPrimary,
    },
    info: {
      main: colors.info,
      contrastText: colors.onPrimary,
    },
  },
})

export default theme
