import { describe, it, expect, vi, beforeEach } from 'vitest'
import { renderHook, act, waitFor } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import React from 'react'
import { useApiMutation } from './useApiMutation'

vi.mock('../utils/notify', () => ({
  notifySuccess: vi.fn(),
  notifyError: vi.fn(),
}))

import { notifySuccess, notifyError } from '../utils/notify'

const createWrapper = () => {
  const queryClient = new QueryClient({
    defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
  })
  return ({ children }: { children: React.ReactNode }) =>
    React.createElement(QueryClientProvider, { client: queryClient }, children)
}

beforeEach(() => {
  vi.clearAllMocks()
})

describe('useApiMutation — success path', () => {
  it('calls mutationFn and shows success toast', async () => {
    const mutationFn = vi.fn().mockResolvedValue({ id: '1' })
    const { result } = renderHook(
      () => useApiMutation({ mutationFn, successMessage: 'Готово' }),
      { wrapper: createWrapper() },
    )

    await act(async () => result.current.mutate(undefined as void))
    await waitFor(() => expect(result.current.isSuccess).toBe(true))

    expect(mutationFn).toHaveBeenCalledOnce()
    expect(notifySuccess).toHaveBeenCalledWith('Готово')
    expect(notifyError).not.toHaveBeenCalled()
  })

  it('does not show success toast when successMessage is omitted', async () => {
    const mutationFn = vi.fn().mockResolvedValue({})
    const { result } = renderHook(() => useApiMutation({ mutationFn }), {
      wrapper: createWrapper(),
    })

    await act(async () => result.current.mutate(undefined as void))
    await waitFor(() => expect(result.current.isSuccess).toBe(true))

    expect(notifySuccess).not.toHaveBeenCalled()
  })

  it('invalidates query keys on success', async () => {
    const queryClient = new QueryClient({
      defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
    })
    const invalidateSpy = vi.spyOn(queryClient, 'invalidateQueries')
    const wrapper = ({ children }: { children: React.ReactNode }) =>
      React.createElement(QueryClientProvider, { client: queryClient }, children)

    const mutationFn = vi.fn().mockResolvedValue({})
    const { result } = renderHook(
      () => useApiMutation({ mutationFn, invalidateKeys: [['runs'], ['experiments']] }),
      { wrapper },
    )

    await act(async () => result.current.mutate(undefined as void))
    await waitFor(() => expect(result.current.isSuccess).toBe(true))

    expect(invalidateSpy).toHaveBeenCalledWith({ queryKey: ['runs'] })
    expect(invalidateSpy).toHaveBeenCalledWith({ queryKey: ['experiments'] })
  })

  it('calls custom onSuccess after built-in handling', async () => {
    const customOnSuccess = vi.fn()
    const mutationFn = vi.fn().mockResolvedValue({ result: 42 })
    const { result } = renderHook(
      () =>
        useApiMutation({
          mutationFn,
          successMessage: 'OK',
          onSuccess: customOnSuccess,
        }),
      { wrapper: createWrapper() },
    )

    await act(async () => result.current.mutate(undefined as void))
    await waitFor(() => expect(result.current.isSuccess).toBe(true))

    expect(notifySuccess).toHaveBeenCalledWith('OK')
    expect(customOnSuccess).toHaveBeenCalledWith({ result: 42 }, undefined)
  })
})

describe('useApiMutation — error path', () => {
  it('shows server error message from response.data.error', async () => {
    const serverError = Object.assign(new Error('net'), {
      response: { data: { error: 'Сервер недоступен' } },
    })
    const mutationFn = vi.fn().mockRejectedValue(serverError)
    const { result } = renderHook(() => useApiMutation({ mutationFn }), {
      wrapper: createWrapper(),
    })

    await act(async () => result.current.mutate(undefined as void))
    await waitFor(() => expect(result.current.isError).toBe(true))

    expect(notifyError).toHaveBeenCalledWith('Сервер недоступен')
  })

  it('does not toast client-side exception text', async () => {
    const mutationFn = vi.fn().mockRejectedValue(new Error('Network error'))
    const { result } = renderHook(() => useApiMutation({ mutationFn }), {
      wrapper: createWrapper(),
    })

    await act(async () => result.current.mutate(undefined as void))
    await waitFor(() => expect(result.current.isError).toBe(true))

    expect(notifyError).toHaveBeenCalledWith('Произошла ошибка')
  })

  it('skips the error toast when notifyOnError is false', async () => {
    const onError = vi.fn()
    const mutationFn = vi.fn().mockRejectedValue(new Error('boom'))
    const { result } = renderHook(
      () => useApiMutation({ mutationFn, notifyOnError: false, onError }),
      { wrapper: createWrapper() },
    )

    await act(async () => result.current.mutate(undefined as void))
    await waitFor(() => expect(result.current.isError).toBe(true))

    expect(notifyError).not.toHaveBeenCalled()
    expect(onError).toHaveBeenCalled()
  })

  it('calls onMutate before the request', async () => {
    const onMutate = vi.fn()
    const mutationFn = vi.fn().mockResolvedValue('ok')
    const { result } = renderHook(() => useApiMutation({ mutationFn, onMutate }), {
      wrapper: createWrapper(),
    })

    await act(async () => result.current.mutate(undefined as void))
    await waitFor(() => expect(result.current.isSuccess).toBe(true))

    expect(onMutate).toHaveBeenCalled()
  })

  it('inlineError clears on start, sets the message and skips the toast', async () => {
    const inlineError = vi.fn()
    const mutationFn = vi.fn().mockRejectedValue({ response: { data: { error: 'Нет прав' } } })
    const { result } = renderHook(() => useApiMutation({ mutationFn, inlineError }), {
      wrapper: createWrapper(),
    })

    await act(async () => result.current.mutate(undefined as void))
    await waitFor(() => expect(result.current.isError).toBe(true))

    expect(inlineError.mock.calls).toEqual([[null], ['Нет прав']])
    expect(notifyError).not.toHaveBeenCalled()
  })

  it('uses errorFallback when error has no message', async () => {
    const mutationFn = vi.fn().mockRejectedValue({})
    const { result } = renderHook(
      () => useApiMutation({ mutationFn, errorFallback: 'Кастомная ошибка' }),
      { wrapper: createWrapper() },
    )

    await act(async () => result.current.mutate(undefined as void))
    await waitFor(() => expect(result.current.isError).toBe(true))

    expect(notifyError).toHaveBeenCalledWith('Кастомная ошибка')
  })

  it('calls custom onError after showing toast', async () => {
    const customOnError = vi.fn()
    const mutationFn = vi.fn().mockRejectedValue(new Error('oops'))
    const { result } = renderHook(
      () => useApiMutation({ mutationFn, onError: customOnError }),
      { wrapper: createWrapper() },
    )

    await act(async () => result.current.mutate(undefined as void))
    await waitFor(() => expect(result.current.isError).toBe(true))

    expect(notifyError).toHaveBeenCalled()
    expect(customOnError).toHaveBeenCalled()
  })
})
