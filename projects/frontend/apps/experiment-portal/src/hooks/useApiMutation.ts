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
   * Show an error toast on failure (default). Pass `false` when the caller
   * renders the error inline, so the user doesn't see it twice.
   */
  notifyOnError?: boolean
  /** Called before the request starts, e.g. to clear a previous inline error. */
  onMutate?: (vars: TVars) => void
  /** Called after built-in error handling. Use to set local error state. */
  onError?: (error: unknown, vars: TVars) => void
}

export function useApiMutation<TData = unknown, TVars = void>({
  mutationFn,
  invalidateKeys,
  successMessage,
  errorFallback = 'Произошла ошибка',
  notifyOnError = true,
  onMutate,
  onSuccess,
  onError,
}: UseApiMutationOptions<TData, TVars>) {
  const queryClient = useQueryClient()

  return useMutation<TData, unknown, TVars>({
    mutationFn,
    onMutate: onMutate ? (vars) => { onMutate(vars) } : undefined,
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
      if (notifyOnError) {
        notifyError(getApiErrorMessage(error, errorFallback))
      }
      onError?.(error, vars)
    },
  })
}
