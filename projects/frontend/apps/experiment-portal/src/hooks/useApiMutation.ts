import { useMutation, useQueryClient } from '@tanstack/react-query'
import type { QueryKey } from '@tanstack/react-query'
import { notifyError, notifySuccess } from '../utils/notify'

interface UseApiMutationOptions<TData, TVars> {
  mutationFn: (vars: TVars) => Promise<TData>
  /** Query keys to invalidate on success. */
  invalidateKeys?: QueryKey[]
  /** Toast shown on success. Omit to skip success toast. */
  successMessage?: string
  /** Fallback error text when the server sends no message. */
  errorFallback?: string
  /** Called after built-in success handling (invalidate + toast). */
  onSuccess?: (data: TData, vars: TVars) => void
  /** Called after built-in error toast. Use to set local error state. */
  onError?: (error: unknown, vars: TVars) => void
}

function extractErrorMessage(error: unknown, fallback: string): string {
  if (error && typeof error === 'object') {
    const e = error as Record<string, any>
    return (
      e?.response?.data?.error ||
      e?.response?.data?.message ||
      e?.message ||
      fallback
    )
  }
  return fallback
}

export function useApiMutation<TData = unknown, TVars = void>({
  mutationFn,
  invalidateKeys,
  successMessage,
  errorFallback = 'Произошла ошибка',
  onSuccess,
  onError,
}: UseApiMutationOptions<TData, TVars>) {
  const queryClient = useQueryClient()

  return useMutation<TData, unknown, TVars>({
    mutationFn,
    onSuccess: (data, vars) => {
      if (invalidateKeys) {
        for (const key of invalidateKeys) {
          queryClient.invalidateQueries({ queryKey: key as readonly unknown[] })
        }
      }
      if (successMessage) {
        notifySuccess(successMessage)
      }
      onSuccess?.(data, vars)
    },
    onError: (error, vars) => {
      notifyError(extractErrorMessage(error, errorFallback))
      onError?.(error, vars)
    },
  })
}
