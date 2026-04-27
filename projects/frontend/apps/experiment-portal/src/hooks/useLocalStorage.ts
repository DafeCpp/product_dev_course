import { useState, useCallback } from 'react'
import type { ZodSchema } from 'zod'

function readFromStorage<T>(key: string, defaultValue: T, schema?: ZodSchema<T>): T {
  try {
    const raw = localStorage.getItem(key)
    if (raw === null) return defaultValue
    const parsed = JSON.parse(raw) as unknown
    if (schema) {
      const result = schema.safeParse(parsed)
      return result.success ? result.data : defaultValue
    }
    return parsed as T
  } catch {
    return defaultValue
  }
}

export function useLocalStorage<T>(
  key: string,
  defaultValue: T,
  schema?: ZodSchema<T>,
): [T, (value: T | ((prev: T) => T)) => void, () => void] {
  const [storedValue, setStoredValue] = useState<T>(() =>
    readFromStorage(key, defaultValue, schema),
  )

  const setValue = useCallback(
    (value: T | ((prev: T) => T)) => {
      setStoredValue((prev) => {
        const next = typeof value === 'function' ? (value as (prev: T) => T)(prev) : value
        try {
          localStorage.setItem(key, JSON.stringify(next))
        } catch {
          // quota exceeded or serialization error — keep in-memory state only
        }
        return next
      })
    },
    [key],
  )

  const removeValue = useCallback(() => {
    setStoredValue(defaultValue)
    localStorage.removeItem(key)
  }, [key, defaultValue])

  return [storedValue, setValue, removeValue]
}
