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
  /**
   * Called after built-in success handling (invalidate + toast). A returned
   * promise is awaited: the mutation stays pending until it settles.
   */
  onSuccess?: (data: TData, vars: TVars) => unknown
  /**
   * Show an error toast on failure (default). Pass `false` when the caller
   * renders the error inline, so the user doesn't see it twice.
   */
  notifyOnError?: boolean
  /**
   * Setter for an inline error block: cleared when the request starts and
   * set to the sanitized message on failure. Implies `notifyOnError: false`.
   */
  inlineError?: (message: string | null) => void
  /**
   * Called after built-in error handling with the sanitized user-facing
   * `message` (server text or `errorFallback`). Use to set inline error state.
   */
  onError?: (error: unknown, vars: TVars, message: string) => void
}

export function useApiMutation<TData = unknown, TVars = void>({
  mutationFn,
  invalidateKeys,
  successMessage,
  errorFallback = 'Произошла ошибка',
  inlineError,
  notifyOnError = inlineError === undefined,
  onSuccess,
  onError,
}: UseApiMutationOptions<TData, TVars>) {
  const queryClient = useQueryClient()

  return useMutation<TData, unknown, TVars>({
    mutationFn,
    onMutate: inlineError ? () => { inlineError(null) } : undefined,
    onSuccess: (data, vars) => {
      if (invalidateKeys) {
        for (const key of invalidateKeys) {
          queryClient.invalidateQueries({ queryKey: key as readonly unknown[] })
        }
      }
      if (successMessage) {
        notifySuccess(successMessage)
      }
      return onSuccess?.(data, vars)
    },
    onError: (error, vars) => {
      const message = getApiErrorMessage(error, errorFallback)
      if (notifyOnError) {
        notifyError(message)
      }
      inlineError?.(message)
      onError?.(error, vars, message)
    },
  })
}
