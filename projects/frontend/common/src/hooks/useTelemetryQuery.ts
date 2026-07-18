import { useQuery } from '@tanstack/react-query'
import type {
  TelemetryAggregatedFn,
  TelemetryAggregatedRecord,
  TelemetryQueryFn,
  TelemetryQueryRecord,
} from '../types/telemetry'

const DEFAULT_MAX_POINTS = 5000
const DEFAULT_PAGE_SIZE = 2000
const DEFAULT_HARD_CAP_LIMIT = 20000

export interface UseTelemetryQueryRawParams {
  mode: 'raw'
  captureSessionId: string
  sensorIds?: string[]
  includeLate?: boolean
  order?: 'asc' | 'desc'
  maxPoints?: number
  pageSize?: number
  hardCapLimit?: number
  onProgress?: (loadedCount: number) => void
}

export interface UseTelemetryQueryAggregatedParams {
  mode: 'aggregated'
  captureSessionId: string
  sensorIds?: string[]
  signal?: string
  timeFrom?: string
  timeTo?: string
  order?: 'asc' | 'desc'
  limit?: number
  hardCapLimit?: number
}

export type UseTelemetryQueryParams = (UseTelemetryQueryRawParams | UseTelemetryQueryAggregatedParams) & {
  enabled?: boolean
  query: TelemetryQueryFn
  aggregated: TelemetryAggregatedFn
}

interface RawQueryData {
  mode: 'raw'
  points: TelemetryQueryRecord[]
  loadedCount: number
  wasTruncated: boolean
}

interface AggregatedQueryData {
  mode: 'aggregated'
  buckets: TelemetryAggregatedRecord[]
  loadedCount: number
}

export type UseTelemetryQueryResult =
  | ({ mode: 'raw' } & RawQueryData & {
        isLoading: boolean
        isFetching: boolean
        error: Error | null
        refetch: () => void
      })
  | ({ mode: 'aggregated' } & AggregatedQueryData & {
        isLoading: boolean
        isFetching: boolean
        error: Error | null
        refetch: () => void
      })

async function fetchRawPaginated(
  query: TelemetryQueryFn,
  params: UseTelemetryQueryRawParams,
): Promise<RawQueryData> {
  const maxPoints = Math.min(params.maxPoints ?? DEFAULT_MAX_POINTS, params.hardCapLimit ?? DEFAULT_HARD_CAP_LIMIT)
  const pageSize = params.pageSize ?? DEFAULT_PAGE_SIZE

  const points: TelemetryQueryRecord[] = []
  let sinceId: number | undefined
  let wasTruncated = false

  while (points.length < maxPoints) {
    const remaining = maxPoints - points.length
    const limit = Math.min(pageSize, remaining)
    const page = await query({
      capture_session_id: params.captureSessionId,
      sensor_id: params.sensorIds,
      since_id: sinceId,
      limit,
      include_late: params.includeLate,
      order: params.order,
    })

    points.push(...page.points)
    params.onProgress?.(points.length)

    if (page.next_since_id === null || page.next_since_id === undefined) {
      break
    }
    if (page.points.length === 0) {
      break
    }
    sinceId = page.next_since_id
    if (points.length >= maxPoints) {
      wasTruncated = true
    }
  }

  return { mode: 'raw', points, loadedCount: points.length, wasTruncated }
}

async function fetchAggregated(
  aggregated: TelemetryAggregatedFn,
  params: UseTelemetryQueryAggregatedParams,
): Promise<AggregatedQueryData> {
  const limit = Math.min(params.limit ?? DEFAULT_MAX_POINTS, params.hardCapLimit ?? DEFAULT_HARD_CAP_LIMIT)
  const response = await aggregated({
    capture_session_id: params.captureSessionId,
    sensor_id: params.sensorIds,
    signal: params.signal,
    time_from: params.timeFrom,
    time_to: params.timeTo,
    limit,
    order: params.order,
  })

  return { mode: 'aggregated', buckets: response.buckets, loadedCount: response.buckets.length }
}

export function useTelemetryQuery(params: UseTelemetryQueryParams): UseTelemetryQueryResult {
  const { enabled = true, query, aggregated } = params

  const queryKey =
    params.mode === 'raw'
      ? [
          'telemetry-query',
          params.captureSessionId,
          params.sensorIds,
          params.includeLate,
          params.order,
          params.maxPoints,
        ]
      : [
          'telemetry-aggregated',
          params.captureSessionId,
          params.sensorIds,
          params.signal,
          params.timeFrom,
          params.timeTo,
          params.order,
          params.limit,
        ]

  const result = useQuery({
    queryKey,
    queryFn: async (): Promise<RawQueryData | AggregatedQueryData> =>
      params.mode === 'raw' ? fetchRawPaginated(query, params) : fetchAggregated(aggregated, params),
    enabled,
  })

  const refetch = () => {
    void result.refetch()
  }

  if (params.mode === 'raw') {
    const data = result.data as RawQueryData | undefined
    return {
      mode: 'raw',
      points: data?.points ?? [],
      loadedCount: data?.loadedCount ?? 0,
      wasTruncated: data?.wasTruncated ?? false,
      isLoading: result.isLoading,
      isFetching: result.isFetching,
      error: result.error as Error | null,
      refetch,
    }
  }

  const data = result.data as AggregatedQueryData | undefined
  return {
    mode: 'aggregated',
    buckets: data?.buckets ?? [],
    loadedCount: data?.loadedCount ?? 0,
    isLoading: result.isLoading,
    isFetching: result.isFetching,
    error: result.error as Error | null,
    refetch,
  }
}
