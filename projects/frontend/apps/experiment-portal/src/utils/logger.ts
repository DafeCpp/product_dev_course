/**
 * Logger utility with DEV-only output.
 * In test/production modes, logs are suppressed.
 */
const isDev = import.meta.env.DEV

function shouldLog(): boolean {
  return isDev
}

export const logger = {
  debug: (message: string, data?: unknown): void => {
    if (!shouldLog()) return
    console.debug(`[DEBUG] ${message}`, data)
  },

  info: (message: string, data?: unknown): void => {
    if (!shouldLog()) return
    console.info(`[INFO] ${message}`, data)
  },

  warn: (message: string, data?: unknown): void => {
    if (!shouldLog()) return
    console.warn(`[WARN] ${message}`, data)
  },

  error: (message: string, error?: unknown): void => {
    if (!shouldLog()) return
    console.error(`[ERROR] ${message}`, error)
  },
}
