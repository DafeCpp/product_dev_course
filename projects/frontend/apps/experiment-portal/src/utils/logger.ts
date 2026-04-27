/** Logger utility with DEV-only output. Suppressed in test and production. */
function shouldLog(): boolean {
  return import.meta.env.DEV
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
