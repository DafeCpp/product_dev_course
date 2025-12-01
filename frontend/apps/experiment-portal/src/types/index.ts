/** Типы для экспериментов и runs */

export interface Experiment {
  id: string
  project_id: string
  name: string
  description?: string
  experiment_type?: string
  created_by: string
  status: 'created' | 'running' | 'completed' | 'failed' | 'archived'
  tags: string[]
  metadata: Record<string, any>
  created_at: string
  updated_at: string
}

export interface Run {
  id: string
  experiment_id: string
  name: string
  parameters: Record<string, any>
  status: 'created' | 'running' | 'completed' | 'failed'
  started_at?: string
  completed_at?: string
  duration_seconds?: number
  notes?: string
  metadata: Record<string, any>
  created_at: string
  updated_at: string
}

export interface ExperimentCreate {
  project_id: string
  name: string
  description?: string
  experiment_type?: string
  tags?: string[]
  metadata?: Record<string, any>
}

export interface ExperimentUpdate {
  name?: string
  description?: string
  experiment_type?: string
  tags?: string[]
  metadata?: Record<string, any>
  status?: string
}

export interface RunCreate {
  name: string
  parameters: Record<string, any>
  notes?: string
  metadata?: Record<string, any>
}

export interface RunUpdate {
  name?: string
  parameters?: Record<string, any>
  notes?: string
  metadata?: Record<string, any>
  status?: string
}

export interface ExperimentsListResponse {
  experiments: Experiment[]
  total: number
  page: number
  page_size: number
}

export interface RunsListResponse {
  runs: Run[]
  total: number
  page: number
  page_size: number
}

