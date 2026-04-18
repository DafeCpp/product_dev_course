export type ScriptType = 'python' | 'bash' | 'javascript'
export type ExecutionStatus = 'pending' | 'running' | 'completed' | 'failed' | 'cancelled' | 'timeout'

export interface ScriptCreate {
  name: string
  description?: string
  target_service: string
  script_type: ScriptType
  script_body: string
  parameters_schema?: Record<string, unknown>
  timeout_sec?: number
}

export interface ScriptUpdate {
  name?: string
  description?: string
  script_body?: string
  parameters_schema?: Record<string, unknown>
  timeout_sec?: number
  is_active?: boolean
}

export interface ScriptsListResponse {
  scripts: Script[]
  total: number
  limit: number
  offset: number
}

export interface ExecutionsListResponse {
  executions: ScriptExecution[]
  total: number
  limit: number
  offset: number
}

export interface Script {
  id: string
  name: string
  description: string | null
  target_service: string
  script_type: ScriptType
  script_body: string
  parameters_schema: Record<string, unknown>
  timeout_sec: number
  is_active: boolean
  created_by: string
  created_at: string
  updated_at: string
}

export interface ScriptExecution {
  id: string
  script_id: string
  status: ExecutionStatus
  parameters: Record<string, unknown>
  target_instance: string | null
  requested_by: string
  started_at: string | null
  finished_at: string | null
  exit_code: number | null
  stdout: string | null
  stderr: string | null
  error_message: string | null
  created_at: string
  updated_at: string
}
