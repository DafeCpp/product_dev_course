import { useState } from 'react'
import type { ZodType } from 'zod'
import { flatFieldErrors } from '../schemas/forms'
import { notifyError } from '../utils/notify'

export interface UseFormErrorsReturn {
  error: string | null
  fieldErrors: Record<string, string | undefined>
  setError: (msg: string | null) => void
  clearErrors: () => void
  validate<T>(schema: ZodType<T>, data: unknown): T | null
}

export function useFormErrors(): UseFormErrorsReturn {
  const [error, setError] = useState<string | null>(null)
  const [fieldErrors, setFieldErrors] = useState<Record<string, string | undefined>>({})

  function clearErrors() {
    setError(null)
    setFieldErrors({})
  }

  function validate<T>(schema: ZodType<T>, data: unknown): T | null {
    const result = schema.safeParse(data)
    if (!result.success) {
      const errors = flatFieldErrors(result.error)
      setFieldErrors(errors)
      const first = Object.values(errors).find(Boolean) ?? 'Проверьте заполнение формы'
      setError(first)
      notifyError(first)
      return null
    }
    clearErrors()
    return result.data
  }

  return { error, fieldErrors, setError, clearErrors, validate }
}
