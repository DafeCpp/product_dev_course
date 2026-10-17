import { useMutation, useQueryClient } from '@tanstack/react-query'
import type { QueryKey } from '@tanstack/react-query'
import { notifyError, notifySuccess } from '../utils/notify'
import { getApiErrorMessage } from '../utils/apiError'

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
  /**
   * Show an error toast on failure. Defaults to `true` without `onError`
   * and `false` with it: a caller that handles the error renders it inline,
   * so the user doesn't see it twice. Pass explicitly to override.
   */
  notifyOnError?: boolean
  /** Called after built-in error handling. Use to set local error state. */
  onError?: (error: unknown, vars: TVars) => void
}

export function useApiMutation<TData = unknown, TVars = void>({
  mutationFn,
  invalidateKeys,
  successMessage,
  errorFallback = 'Произошла ошибка',
  notifyOnError,
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
      if (notifyOnError ?? onError === undefined) {
        notifyError(getApiErrorMessage(error, errorFallback, { includeClientMessage: true }))
      }
      onError?.(error, vars)
    },
  })
}
