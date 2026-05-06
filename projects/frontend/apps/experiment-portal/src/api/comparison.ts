/** Comparison API */
import type { ComparisonResponse } from '../types'
import { apiPost } from './client'

export const comparisonApi = {
  compare: async (
    experimentId: string,
    body: { run_ids: string[]; metric_names: string[] }
  ): Promise<ComparisonResponse> => {
    return await apiPost(`/api/v1/experiments/${experimentId}/compare`, body)
  },

  exportUrl: (
    experimentId: string,
    params: { run_ids: string[]; metric_names: string[]; format: 'csv' | 'json' }
  ): string => {
    const base = `/api/v1/experiments/${experimentId}/compare/export`
    const q = new URLSearchParams({
      run_ids: params.run_ids.join(','),
      names: params.metric_names.join(','),
      format: params.format,
    })
    return `${base}?${q.toString()}`
  },
}
