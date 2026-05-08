import { useState } from 'react'
import { useQuery, useQueryClient } from '@tanstack/react-query'
import { useApiMutation } from '../hooks/useApiMutation'
import { scriptsApi } from '../api/scripts'
import type { Script, ScriptExecution } from '../types/scripts'
import { usePermissions } from '../hooks/usePermissions'
import PermissionGate from '../components/PermissionGate'
import { Loading, Error as ErrorComponent, EmptyState, MaterialSelect, LiveSwitch, ListSearchIcon, SensorIcon } from '../components/common'
import { notifySuccess, notifyError } from '../utils/notify'
import ScriptFormModal from './scripts/ScriptFormModal'
import ScriptExecuteModal from './scripts/ScriptExecuteModal'
import ScriptExecDetailModal from './scripts/ScriptExecDetailModal'
import ScriptsTable from './scripts/ScriptsTable'
import ExecutionsTable from './scripts/ExecutionsTable'
import './Scripts.scss'

type TabId = 'registry' | 'executions'

// ---------------------------------------------------------------------------
// Registry tab
// ---------------------------------------------------------------------------

interface RegistryTabProps {
  onScriptExecuted: () => void
}

function RegistryTab({ onScriptExecuted: _onScriptExecuted }: RegistryTabProps) {
  void _onScriptExecuted
  const queryClient = useQueryClient()
  const [filterService, setFilterService] = useState('')
  const [filterActive, setFilterActive] = useState<boolean | undefined>(undefined)
  const [editingScript, setEditingScript] = useState<Script | null | undefined>(undefined)
  // undefined = modal closed, null = create mode, Script = edit mode

  const filters = {
    target_service: filterService || undefined,
    is_active: filterActive,
  }

  const { data, isLoading, error } = useQuery({
    queryKey: ['scripts', filters],
    queryFn: async () => {
      try {
        return await scriptsApi.listScripts(filters)
      } catch (err: unknown) {
        const e = err as { response?: { data?: { error?: string; message?: string } }; message?: string }
        notifyError(
          e?.response?.data?.error ||
            e?.response?.data?.message ||
            e?.message ||
            'Ошибка загрузки скриптов'
        )
        throw err
      }
    },
    staleTime: 15_000,
    refetchOnWindowFocus: false,
  })

  const toggleActiveMutation = useApiMutation<Script, Script>({
    mutationFn: (script) => scriptsApi.updateScript(script.id, { is_active: !script.is_active }),
    invalidateKeys: [['scripts']],
    errorFallback: 'Не удалось изменить статус скрипта',
    onSuccess: (updated) => notifySuccess(updated.is_active ? 'Скрипт активирован' : 'Скрипт деактивирован'),
  })

  const deleteMutation = useApiMutation<unknown, string>({
    mutationFn: (id) => scriptsApi.deleteScript(id),
    invalidateKeys: [['scripts']],
    successMessage: 'Скрипт удалён',
    errorFallback: 'Не удалось удалить скрипт',
  })

  const scripts = data?.scripts ?? []

  return (
    <>
      <div className="filter-capsule scripts-filter-capsule">
        <div className="filter-capsule__search filter-capsule__search--constrained">
          <ListSearchIcon />
          <input
            type="text"
            placeholder="Сервис..."
            value={filterService}
            onChange={(e) => setFilterService(e.target.value)}
            aria-label="Фильтр по сервису"
          />
        </div>
        <LiveSwitch
          live={filterActive === true}
          onChange={(on) => setFilterActive(on ? true : undefined)}
          labelOn="Активные"
          labelOff="Все"
        />
        <PermissionGate permission="scripts.manage" system>
          <button
            className="btn btn-primary btn-sm filter-capsule__btn"
            onClick={() => setEditingScript(null)}
          >
            + Создать
          </button>
        </PermissionGate>
      </div>

      {isLoading && <Loading message="Загрузка скриптов..." />}
      {error && (
        <ErrorComponent
          message={error instanceof Error ? error.message : 'Ошибка загрузки скриптов'}
        />
      )}

      {!isLoading && !error && scripts.length === 0 && (
        <EmptyState message="Скриптов не найдено" />
      )}

      {!isLoading && !error && scripts.length > 0 && (
        <ScriptsTable
          scripts={scripts}
          toggleActivePending={toggleActiveMutation.isPending}
          deletePending={deleteMutation.isPending}
          onEdit={(s) => setEditingScript(s)}
          onToggleActive={(s) => toggleActiveMutation.mutate(s)}
          onDelete={(s) => {
            if (confirm(`Удалить скрипт «${s.name}»?`)) {
              deleteMutation.mutate(s.id)
            }
          }}
        />
      )}

      {editingScript !== undefined && (
        <ScriptFormModal
          script={editingScript}
          onClose={() => setEditingScript(undefined)}
          onSaved={() => {
            setEditingScript(undefined)
            queryClient.invalidateQueries({ queryKey: ['scripts'] })
          }}
        />
      )}
    </>
  )
}

// ---------------------------------------------------------------------------
// Executions tab
// ---------------------------------------------------------------------------

interface ExecutionsTabProps {
  onTabChange: (tab: TabId) => void
}

function ExecutionsTab({ onTabChange: _onTabChange }: ExecutionsTabProps) {
  void _onTabChange
  const queryClient = useQueryClient()
  const [statusFilter, setStatusFilter] = useState('')
  const [selectedExecution, setSelectedExecution] = useState<ScriptExecution | null>(null)
  const [showExecuteModal, setShowExecuteModal] = useState(false)

  const executionFilters = {
    status: statusFilter || undefined,
  }

  const { data: executionsData, isLoading: exLoading, error: exError } = useQuery({
    queryKey: ['executions', executionFilters],
    queryFn: async () => {
      try {
        return await scriptsApi.listExecutions(executionFilters)
      } catch (err: unknown) {
        const e = err as { response?: { data?: { error?: string; message?: string } }; message?: string }
        notifyError(
          e?.response?.data?.error ||
            e?.response?.data?.message ||
            e?.message ||
            'Ошибка загрузки выполнений'
        )
        throw err
      }
    },
    staleTime: 5_000,
    refetchOnWindowFocus: false,
    refetchInterval: (query) => {
      const executions = query.state.data?.executions
      if (
        executions?.some(
          (e) => e.status === 'pending' || e.status === 'running'
        )
      ) {
        return 2_000
      }
      return false
    },
  })

  const { data: activeScripts } = useQuery({
    queryKey: ['scripts', { is_active: true }],
    queryFn: () => scriptsApi.listScripts({ is_active: true }),
    staleTime: 30_000,
    enabled: showExecuteModal,
  })

  const executions = executionsData?.executions ?? []

  // Build a script id -> name map from cached scripts query
  const scriptMap = useQuery({
    queryKey: ['scripts', {}],
    queryFn: () => scriptsApi.listScripts(),
    staleTime: 60_000,
  }).data?.scripts.reduce<Record<string, string>>((acc, s) => {
    acc[s.id] = s.name
    return acc
  }, {}) ?? {}

  const scriptName = (id: string) => scriptMap[id] ?? id

  return (
    <>
      <div className="filter-capsule scripts-filter-capsule">
        <MaterialSelect
          id="exec-filter-status"
          label="Статус"
          value={statusFilter}
          onChange={(v) => setStatusFilter(v)}
          variant="pill"
          icon={<SensorIcon />}
        >
          <option value="">Все</option>
          <option value="pending">Ожидание</option>
          <option value="running">Выполняется</option>
          <option value="completed">Завершён</option>
          <option value="failed">Ошибка</option>
          <option value="cancelled">Отменён</option>
          <option value="timeout">Таймаут</option>
        </MaterialSelect>
        <PermissionGate permission="scripts.execute" system>
          <button
            className="btn btn-primary btn-sm filter-capsule__btn"
            onClick={() => setShowExecuteModal(true)}
          >
            + Запустить
          </button>
        </PermissionGate>
      </div>

      {exLoading && <Loading message="Загрузка выполнений..." />}
      {exError && (
        <ErrorComponent
          message={exError instanceof Error ? exError.message : 'Ошибка загрузки выполнений'}
        />
      )}

      {!exLoading && !exError && executions.length === 0 && (
        <EmptyState message="Выполнений не найдено" />
      )}

      {!exLoading && !exError && executions.length > 0 && (
        <ExecutionsTable
          executions={executions}
          scriptName={scriptName}
          onRowClick={(ex) => setSelectedExecution(ex)}
        />
      )}

      {selectedExecution && (
        <ScriptExecDetailModal
          execution={selectedExecution}
          scriptName={scriptName(selectedExecution.script_id)}
          onClose={() => setSelectedExecution(null)}
          onCancelled={() => {
            setSelectedExecution(null)
            queryClient.invalidateQueries({ queryKey: ['executions'] })
          }}
        />
      )}

      {showExecuteModal && (
        <ScriptExecuteModal
          scripts={activeScripts?.scripts ?? []}
          onClose={() => setShowExecuteModal(false)}
          onExecuted={() => {
            setShowExecuteModal(false)
            queryClient.invalidateQueries({ queryKey: ['executions'] })
          }}
        />
      )}
    </>
  )
}

// ---------------------------------------------------------------------------
// Scripts page
// ---------------------------------------------------------------------------

function Scripts() {
  const { hasSystemPermission, isLoading: permissionsLoading } = usePermissions()
  const [activeTab, setActiveTab] = useState<TabId>('registry')

  if (permissionsLoading) {
    return <Loading message="Проверка прав доступа..." />
  }

  const canExecute = hasSystemPermission('scripts.execute')
  const canManage = hasSystemPermission('scripts.manage')

  if (!canExecute && !canManage) {
    return (
      <div className="scripts-page">
        <h2 className="scripts-page__title">Скрипты</h2>
        <div className="scripts-page__no-access">Нет доступа</div>
      </div>
    )
  }

  return (
    <div className="scripts-page">
      <h2 className="scripts-page__title">Скрипты</h2>

      <div className="scripts-tabs">
        <button
          className={`scripts-tabs__tab${activeTab === 'registry' ? ' scripts-tabs__tab--active' : ''}`}
          onClick={() => setActiveTab('registry')}
        >
          Реестр
        </button>
        <button
          className={`scripts-tabs__tab${activeTab === 'executions' ? ' scripts-tabs__tab--active' : ''}`}
          onClick={() => setActiveTab('executions')}
        >
          Выполнения
        </button>
      </div>

      {activeTab === 'registry' && (
        <RegistryTab onScriptExecuted={() => setActiveTab('executions')} />
      )}
      {activeTab === 'executions' && (
        <ExecutionsTab onTabChange={setActiveTab} />
      )}
    </div>
  )
}

export default Scripts
