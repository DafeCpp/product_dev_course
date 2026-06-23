import { createTheme } from '@mui/material/styles'

const theme = createTheme({
  palette: {
    primary: {
      main: '#4f46e5',
      dark: '#4338ca',
      light: '#818cf8',
      contrastText: '#fff',
    },
    error: {
      main: '#dc2626',
      contrastText: '#fff',
    },
    warning: {
      main: '#d97706',
      contrastText: '#fff',
    },
    success: {
      main: '#16a34a',
      contrastText: '#fff',
    },
    info: {
      main: '#0284c7',
      contrastText: '#fff',
    },
  },
})

export default theme
