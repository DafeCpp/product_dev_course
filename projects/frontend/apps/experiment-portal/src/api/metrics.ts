/** Metrics API */
import type {
  RunMetricsResponse,
  RunMetricsListResponse,
  MetricSummaryResponse,
  MetricAggregationsResponse,
} from '../types'
import { apiGet, apiPost } from './client'

export const metricsApi = {
  /** Legacy: returns series grouped by name. Kept for backward compat. */
  query: async (
    runId: string,
    params?: {
      name?: string
      from_step?: number
      to_step?: number
    }
  ): Promise<RunMetricsResponse> => {
    return await apiGet(`/api/v1/runs/${runId}/metrics`, { params })
  },

  /** Batch insert metrics for a run. */
  record: async (
    runId: string,
    metrics: Array<{ name: string; step: number; value: number }>
  ): Promise<{ accepted: number }> => {
    return await apiPost(`/api/v1/runs/${runId}/metrics`, { metrics })
  },

  /** List raw metric points with optional filtering. */
  list: async (
    runId: string,
    params?: {
      names?: string
      from_step?: number
      to_step?: number
      order?: string
      limit?: number
      offset?: number
    }
  ): Promise<RunMetricsListResponse> => {
    return await apiGet(`/api/v1/runs/${runId}/metrics`, { params })
  },

  /** Summary per metric name: last value, min, avg, max. */
  summary: async (
    runId: string,
    names?: string
  ): Promise<MetricSummaryResponse> => {
    return await apiGet(`/api/v1/runs/${runId}/metrics/summary`, {
      params: names ? { names } : undefined,
    })
  },

  /** Step-bucketed aggregations. Use for large datasets (>10K steps). */
  aggregations: async (
    runId: string,
    params: {
      names: string
      from_step?: number
      to_step?: number
      bucket_size?: number
    }
  ): Promise<MetricAggregationsResponse> => {
    return await apiGet(`/api/v1/runs/${runId}/metrics/aggregations`, { params })
  },
}
