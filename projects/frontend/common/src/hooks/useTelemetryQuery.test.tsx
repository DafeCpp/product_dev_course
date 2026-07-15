import { describe, it, expect, vi } from 'vitest'
import { renderHook, waitFor } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import type { ReactNode } from 'react'
import { useTelemetryQuery } from './useTelemetryQuery'
import type { TelemetryQueryRecord, TelemetryQueryResponse, TelemetryAggregatedResponse } from '../types/telemetry'

function makeWrapper() {
  const client = new QueryClient({ defaultOptions: { queries: { retry: false } } })
  return ({ children }: { children: ReactNode }) => (
    <QueryClientProvider client={client}>{children}</QueryClientProvider>
  )
}

function makeRecord(id: number): TelemetryQueryRecord {
  return {
    id,
    sensor_id: 's1',
    project_id: 'p1',
    timestamp: `2024-01-01T00:00:${String(id).padStart(2, '0')}.000Z`,
    raw_value: id,
    physical_value: id,
    run_id: null,
    capture_session_id: 'cs1',
    meta: {},
  }
}

describe('useTelemetryQuery', () => {
  it('pages through the injected query fn until next_since_id is null', async () => {
    const query = vi.fn(async ({ since_id }: { since_id?: number }): Promise<TelemetryQueryResponse> => {
      if (!since_id) {
        return { points: [makeRecord(1), makeRecord(2)], next_since_id: 2 }
      }
      return { points: [makeRecord(3)], next_since_id: null }
    })
    const aggregated = vi.fn()

    const { result } = renderHook(
      () => useTelemetryQuery({ mode: 'raw', captureSessionId: 'cs1', query, aggregated }),
      { wrapper: makeWrapper() },
    )

    await waitFor(() => expect(result.current.isLoading).toBe(false))
    expect(result.current.mode).toBe('raw')
    if (result.current.mode === 'raw') {
      expect(result.current.points.map((p) => p.id)).toEqual([1, 2, 3])
      expect(result.current.wasTruncated).toBe(false)
    }
    expect(query).toHaveBeenCalledTimes(2)
  })

  it('respects hardCapLimit and marks wasTruncated when more data exists', async () => {
    const query = vi.fn(async (): Promise<TelemetryQueryResponse> => ({
      points: [makeRecord(1), makeRecord(2)],
      next_since_id: 2,
    }))
    const aggregated = vi.fn()

    const { result } = renderHook(
      () =>
        useTelemetryQuery({
          mode: 'raw',
          captureSessionId: 'cs1',
          maxPoints: 4,
          pageSize: 2,
          query,
          aggregated,
        }),
      { wrapper: makeWrapper() },
    )

    await waitFor(() => expect(result.current.isLoading).toBe(false))
    if (result.current.mode === 'raw') {
      expect(result.current.loadedCount).toBe(4)
      expect(result.current.wasTruncated).toBe(true)
    }
  })

  it("shrinks the last page's limit to fit the remaining budget", async () => {
    const query = vi.fn(async ({ limit }: { limit?: number }): Promise<TelemetryQueryResponse> => {
      if (query.mock.calls.length === 1) {
        return { points: [makeRecord(1), makeRecord(2), makeRecord(3)], next_since_id: 3 }
      }
      return { points: Array.from({ length: limit ?? 0 }, (_, i) => makeRecord(4 + i)), next_since_id: null }
    })
    const aggregated = vi.fn()

    const { result } = renderHook(
      () =>
        useTelemetryQuery({
          mode: 'raw',
          captureSessionId: 'cs1',
          maxPoints: 5,
          pageSize: 3,
          query,
          aggregated,
        }),
      { wrapper: makeWrapper() },
    )

    await waitFor(() => expect(result.current.isLoading).toBe(false))
    expect(query).toHaveBeenNthCalledWith(2, expect.objectContaining({ limit: 2 }))
  })

  it('invokes onProgress after each page', async () => {
    const onProgress = vi.fn()
    const query = vi.fn(async ({ since_id }: { since_id?: number }): Promise<TelemetryQueryResponse> => {
      if (!since_id) return { points: [makeRecord(1)], next_since_id: 1 }
      return { points: [makeRecord(2)], next_since_id: null }
    })
    const aggregated = vi.fn()

    const { result } = renderHook(
      () => useTelemetryQuery({ mode: 'raw', captureSessionId: 'cs1', query, aggregated, onProgress }),
      { wrapper: makeWrapper() },
    )

    await waitFor(() => expect(result.current.isLoading).toBe(false))
    expect(onProgress).toHaveBeenNthCalledWith(1, 1)
    expect(onProgress).toHaveBeenNthCalledWith(2, 2)
  })

  it('calls aggregated exactly once in aggregated mode with the clamped limit', async () => {
    const query = vi.fn()
    const aggregated = vi.fn(
      async (): Promise<TelemetryAggregatedResponse> => ({
        buckets: [{ bucket: 'b1', sensor_id: 's1', signal: null, capture_session_id: 'cs1', sample_count: 1, avg_raw: 1, min_raw: 1, max_raw: 1, avg_physical: 1, min_physical: 1, max_physical: 1 }],
        bucket_interval: '1m',
      }),
    )

    const { result } = renderHook(
      () =>
        useTelemetryQuery({
          mode: 'aggregated',
          captureSessionId: 'cs1',
          limit: 100,
          hardCapLimit: 50,
          query,
          aggregated,
        }),
      { wrapper: makeWrapper() },
    )

    await waitFor(() => expect(result.current.isLoading).toBe(false))
    expect(aggregated).toHaveBeenCalledTimes(1)
    expect(aggregated).toHaveBeenCalledWith(expect.objectContaining({ limit: 50 }))
    if (result.current.mode === 'aggregated') {
      expect(result.current.buckets).toHaveLength(1)
    }
  })

  it('does not fetch anything until refetch() is called when enabled is false', async () => {
    const query = vi.fn(async (): Promise<TelemetryQueryResponse> => ({ points: [], next_since_id: null }))
    const aggregated = vi.fn()

    const { result } = renderHook(
      () => useTelemetryQuery({ mode: 'raw', captureSessionId: 'cs1', enabled: false, query, aggregated }),
      { wrapper: makeWrapper() },
    )

    await new Promise((r) => setTimeout(r, 20))
    expect(query).not.toHaveBeenCalled()

    result.current.refetch()
    await waitFor(() => expect(query).toHaveBeenCalledTimes(1))
  })

  it('refetches from scratch when the query key changes (e.g. captureSessionId)', async () => {
    const query = vi.fn(async (): Promise<TelemetryQueryResponse> => ({ points: [makeRecord(1)], next_since_id: null }))
    const aggregated = vi.fn()

    const { rerender } = renderHook(
      ({ captureSessionId }) => useTelemetryQuery({ mode: 'raw', captureSessionId, query, aggregated }),
      { wrapper: makeWrapper(), initialProps: { captureSessionId: 'cs1' } },
    )

    await waitFor(() => expect(query).toHaveBeenCalledTimes(1))
    rerender({ captureSessionId: 'cs2' })

    await waitFor(() => expect(query).toHaveBeenCalledTimes(2))
    expect(query).toHaveBeenNthCalledWith(2, expect.objectContaining({ capture_session_id: 'cs2' }))
  })

  it('surfaces a mid-pagination error with no partial data committed', async () => {
    const query = vi.fn(async ({ since_id }: { since_id?: number }): Promise<TelemetryQueryResponse> => {
      if (!since_id) return { points: [makeRecord(1)], next_since_id: 1 }
      throw new Error('boom')
    })
    const aggregated = vi.fn()

    const { result } = renderHook(
      () => useTelemetryQuery({ mode: 'raw', captureSessionId: 'cs1', query, aggregated }),
      { wrapper: makeWrapper() },
    )

    await waitFor(() => expect(result.current.error).not.toBeNull())
    if (result.current.mode === 'raw') {
      expect(result.current.points).toEqual([])
    }
  })
})
