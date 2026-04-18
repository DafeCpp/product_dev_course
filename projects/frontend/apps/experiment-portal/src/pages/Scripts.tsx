import { useState } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { format } from 'date-fns'
import { scriptsApi } from '../api/scripts'
import type { Script, ScriptExecution, ExecutionStatus, ScriptType } from '../types/scripts'
import { usePermissions } from '../hooks/usePermissions'
import PermissionGate from '../components/PermissionGate'
import { Loading, Error as ErrorComponent, EmptyState } from '../components/common'
import Modal from '../components/Modal'
import { notifySuccess, notifyError } from '../utils/notify'
import './Scripts.scss'

type TabId = 'registry' | 'executions'

// ---------------------------------------------------------------------------
// Status badge helpers
// ---------------------------------------------------------------------------

function executionStatusClass(status: ExecutionStatus): string {
  switch (status) {
    case 'completed':
      return 'exec-status--completed'
    case 'failed':
    case 'timeout':
      return 'exec-status--failed'
    case 'running':
      return 'exec-status--running'
    case 'cancelled':
      return 'exec-status--cancelled'
    default:
      return 'exec-status--pending'
  }
}

function executionStatusLabel(status: ExecutionStatus): string {
  const map: Record<ExecutionStatus, string> = {
    pending: 'Ожидание',
    running: 'Выполняется',
    completed: 'Завершён',
    failed: 'Ошибка',
    cancelled: 'Отменён',
    timeout: 'Таймаут',
  }
  return map[status] ?? status
}

function calcDuration(started: string | null, finished: string | null): string {
  if (!started || !finished) return '—'
  const diff = (new Date(finished).getTime() - new Date(started).getTime()) / 1000
  return `${diff.toFixed(1)} с`
}

// ---------------------------------------------------------------------------
// Script form modal (create / edit)
// ---------------------------------------------------------------------------

interface ScriptFormData {
  name: string
  description: string
  target_service: string
  script_type: ScriptType
  script_body: string
  timeout_sec: number
  parameters_schema_raw: string
}

const EMPTY_FORM: ScriptFormData = {
  name: '',
  description: '',
  target_service: '',
  script_type: 'python',
  script_body: '',
  timeout_sec: 30,
  parameters_schema_raw: '',
}

function scriptToForm(s: Script): ScriptFormData {
  return {
    name: s.name,
    description: s.description ?? '',
    target_service: s.target_service,
    script_type: s.script_type,
    script_body: s.script_body,
    timeout_sec: s.timeout_sec,
    parameters_schema_raw:
      Object.keys(s.parameters_schema).length > 0
        ? JSON.stringify(s.parameters_schema, null, 2)
        : '',
  }
}

interface ScriptFormModalProps {
  script: Script | null
  onClose: () => void
  onSaved: () => void
}

function ScriptFormModal({ script, onClose, onSaved }: ScriptFormModalProps) {
  const isEdit = script !== null
  const [form, setForm] = useState<ScriptFormData>(isEdit ? scriptToForm(script) : EMPTY_FORM)
  const [errors, setErrors] = useState<Partial<Record<keyof ScriptFormData, string>>>({})

  const set = <K extends keyof ScriptFormData>(key: K, value: ScriptFormData[K]) => {
    setForm((prev) => ({ ...prev, [key]: value }))
    setErrors((prev) => ({ ...prev, [key]: undefined }))
  }

  const validate = (): boolean => {
    const next: typeof errors = {}
    if (!form.name.trim()) next.name = 'Обязательное поле'
    if (!form.target_service.trim()) next.target_service = 'Обязательное поле'
    if (form.parameters_schema_raw.trim()) {
      try {
        const parsed = JSON.parse(form.parameters_schema_raw)
        if (typeof parsed !== 'object' || Array.isArray(parsed) || parsed === null) {
          next.parameters_schema_raw = 'Должен быть JSON-объект'
        }
      } catch {
        next.parameters_schema_raw = 'Невалидный JSON'
      }
    }
    setErrors(next)
    return Object.keys(next).length === 0
  }

  const saveMutation = useMutation({
    mutationFn: async () => {
      const parameters_schema: Record<string, unknown> = form.parameters_schema_raw.trim()
        ? (JSON.parse(form.parameters_schema_raw) as Record<string, unknown>)
        : {}
      const payload = {
        name: form.name.trim(),
        description: form.description.trim() || undefined,
        target_service: form.target_service.trim(),
        script_type: form.script_type,
        script_body: form.script_body,
        timeout_sec: form.timeout_sec,
        parameters_schema,
      }
      if (isEdit) {
        return scriptsApi.updateScript(script.id, payload)
      }
      return scriptsApi.createScript(payload)
    },
    onSuccess: () => {
      notifySuccess(isEdit ? 'Скрипт обновлён' : 'Скрипт создан')
      onSaved()
    },
    onError: (err: unknown) => {
      const e = err as { response?: { data?: { error?: string; message?: string } }; message?: string }
      notifyError(
        e?.response?.data?.error ||
          e?.response?.data?.message ||
          e?.message ||
          'Не удалось сохранить скрипт'
      )
    },
  })

  const handleSubmit = () => {
    if (validate()) saveMutation.mutate()
  }

  return (
    <Modal
      isOpen
      onClose={onClose}
      title={isEdit ? 'Редактировать скрипт' : 'Создать скрипт'}
      className="scripts-form-modal"
      disabled={saveMutation.isPending}
    >
      <div className="scripts-form">
        <div className="form-group">
          <label htmlFor="sf-name">Название *</label>
          <input
            id="sf-name"
            type="text"
            value={form.name}
            onChange={(e) => set('name', e.target.value)}
          />
          {errors.name && <small className="field-error">{errors.name}</small>}
        </div>

        <div className="form-group">
          <label htmlFor="sf-description">Описание</label>
          <input
            id="sf-description"
            type="text"
            value={form.description}
            onChange={(e) => set('description', e.target.value)}
          />
        </div>

        <div className="form-row">
          <div className="form-group">
            <label htmlFor="sf-service">Сервис *</label>
            <input
              id="sf-service"
              type="text"
              placeholder="experiment-service"
              value={form.target_service}
              onChange={(e) => set('target_service', e.target.value)}
            />
            {errors.target_service && (
              <small className="field-error">{errors.target_service}</small>
            )}
          </div>

          <div className="form-group">
            <label htmlFor="sf-type">Тип</label>
            <select
              id="sf-type"
              value={form.script_type}
              onChange={(e) => set('script_type', e.target.value as ScriptType)}
            >
              <option value="python">Python</option>
              <option value="bash">Bash</option>
              <option value="javascript">JavaScript</option>
            </select>
          </div>

          <div className="form-group form-group--narrow">
            <label htmlFor="sf-timeout">Таймаут (сек)</label>
            <input
              id="sf-timeout"
              type="number"
              min={1}
              value={form.timeout_sec}
              onChange={(e) => set('timeout_sec', Number(e.target.value))}
            />
          </div>
        </div>

        <div className="form-group">
          <label htmlFor="sf-body">Тело скрипта</label>
          <textarea
            id="sf-body"
            rows={8}
            value={form.script_body}
            onChange={(e) => set('script_body', e.target.value)}
            className="scripts-form__code"
          />
        </div>

        <div className="form-group">
          <label htmlFor="sf-schema">Схема параметров (JSON-объект)</label>
          <textarea
            id="sf-schema"
            rows={4}
            placeholder={'{"param1": {"type": "string"}}'}
            value={form.parameters_schema_raw}
            onChange={(e) => set('parameters_schema_raw', e.target.value)}
            className="scripts-form__code"
          />
          {errors.parameters_schema_raw && (
            <small className="field-error">{errors.parameters_schema_raw}</small>
          )}
        </div>

        <div className="modal-footer">
          <button className="btn btn-secondary btn-sm" onClick={onClose} disabled={saveMutation.isPending}>
            Отмена
          </button>
          <button
            className="btn btn-primary btn-sm"
            onClick={handleSubmit}
            disabled={saveMutation.isPending}
          >
            {saveMutation.isPending ? 'Сохранение...' : isEdit ? 'Сохранить' : 'Создать'}
          </button>
        </div>
      </div>
    </Modal>
  )
}

// ---------------------------------------------------------------------------
// Execute script modal (multi-step)
// ---------------------------------------------------------------------------

interface ExecuteScriptModalProps {
  scripts: Script[]
  onClose: () => void
  onExecuted: () => void
}

function ExecuteScriptModal({ scripts, onClose, onExecuted }: ExecuteScriptModalProps) {
  const [step, setStep] = useState<1 | 2 | 3>(1)
  const [selectedScript, setSelectedScript] = useState<Script | null>(null)
  const [paramsRaw, setParamsRaw] = useState('')
  const [paramsError, setParamsError] = useState('')

  const executeMutation = useMutation({
    mutationFn: async () => {
      if (!selectedScript) throw new Error('Скрипт не выбран')
      let parameters: Record<string, unknown> = {}
      if (paramsRaw.trim()) {
        parameters = JSON.parse(paramsRaw) as Record<string, unknown>
      }
      return scriptsApi.executeScript(selectedScript.id, { parameters })
    },
    onSuccess: () => {
      notifySuccess('Скрипт запущен')
      onExecuted()
    },
    onError: (err: unknown) => {
      const e = err as { response?: { data?: { error?: string; message?: string } }; message?: string }
      notifyError(
        e?.response?.data?.error ||
          e?.response?.data?.message ||
          e?.message ||
          'Не удалось запустить скрипт'
      )
    },
  })

  const goToStep2 = () => {
    if (!selectedScript) {
      notifyError('Выберите скрипт')
      return
    }
    setStep(2)
  }

  const goToStep3 = () => {
    const hasSchema =
      selectedScript && Object.keys(selectedScript.parameters_schema).length > 0
    if (hasSchema && paramsRaw.trim()) {
      try {
        const parsed = JSON.parse(paramsRaw)
        if (typeof parsed !== 'object' || Array.isArray(parsed) || parsed === null) {
          setParamsError('Должен быть JSON-объект')
          return
        }
      } catch {
        setParamsError('Невалидный JSON')
        return
      }
    }
    setParamsError('')
    setStep(3)
  }

  const stepTitle = step === 1 ? 'Выбор скрипта' : step === 2 ? 'Параметры' : 'Подтверждение'
  const hasSchema =
    selectedScript && Object.keys(selectedScript.parameters_schema).length > 0

  let parsedParams: Record<string, unknown> = {}
  if (paramsRaw.trim()) {
    try {
      parsedParams = JSON.parse(paramsRaw) as Record<string, unknown>
    } catch {
      // ignore in summary step
    }
  }

  return (
    <Modal
      isOpen
      onClose={onClose}
      title={`Запустить скрипт — шаг ${step}: ${stepTitle}`}
      className="scripts-execute-modal"
      disabled={executeMutation.isPending}
    >
      <div className="scripts-execute">
        {/* Step indicators */}
        <div className="scripts-execute__steps">
          {([1, 2, 3] as const).map((s) => (
            <span
              key={s}
              className={`scripts-execute__step${step === s ? ' scripts-execute__step--active' : ''}${step > s ? ' scripts-execute__step--done' : ''}`}
            >
              {s}
            </span>
          ))}
        </div>

        {/* Step 1: select script */}
        {step === 1 && (
          <div className="form-group">
            <label htmlFor="exec-script">Скрипт</label>
            <select
              id="exec-script"
              value={selectedScript?.id ?? ''}
              onChange={(e) => {
                const found = scripts.find((s) => s.id === e.target.value) ?? null
                setSelectedScript(found)
                setParamsRaw('')
                setParamsError('')
              }}
            >
              <option value="">— Выберите скрипт —</option>
              {scripts.map((s) => (
                <option key={s.id} value={s.id}>
                  {s.name} ({s.target_service})
                </option>
              ))}
            </select>
          </div>
        )}

        {/* Step 2: params */}
        {step === 2 && (
          <div className="form-group">
            {hasSchema ? (
              <>
                <label htmlFor="exec-params">Параметры (JSON)</label>
                <textarea
                  id="exec-params"
                  rows={6}
                  placeholder={JSON.stringify(selectedScript!.parameters_schema, null, 2)}
                  value={paramsRaw}
                  onChange={(e) => {
                    setParamsRaw(e.target.value)
                    setParamsError('')
                  }}
                  className="scripts-form__code"
                />
                {paramsError && <small className="field-error">{paramsError}</small>}
              </>
            ) : (
              <p className="scripts-execute__no-params">Нет параметров для этого скрипта.</p>
            )}
          </div>
        )}

        {/* Step 3: confirm */}
        {step === 3 && selectedScript && (
          <div className="scripts-execute__summary">
            <div className="scripts-detail__row">
              <span className="scripts-detail__label">Скрипт</span>
              <span className="scripts-detail__value">{selectedScript.name}</span>
            </div>
            <div className="scripts-detail__row">
              <span className="scripts-detail__label">Сервис</span>
              <span className="scripts-detail__value">{selectedScript.target_service}</span>
            </div>
            <div className="scripts-detail__row">
              <span className="scripts-detail__label">Таймаут</span>
              <span className="scripts-detail__value">{selectedScript.timeout_sec} с</span>
            </div>
            <div className="scripts-detail__row">
              <span className="scripts-detail__label">Параметры</span>
              <pre className="scripts-detail__pre">
                {Object.keys(parsedParams).length > 0
                  ? JSON.stringify(parsedParams, null, 2)
                  : '{}'}
              </pre>
            </div>
          </div>
        )}

        <div className="modal-footer">
          {step > 1 && (
            <button
              className="btn btn-secondary btn-sm"
              onClick={() => setStep((s) => (s - 1) as 1 | 2 | 3)}
              disabled={executeMutation.isPending}
            >
              Назад
            </button>
          )}
          <button className="btn btn-secondary btn-sm" onClick={onClose} disabled={executeMutation.isPending}>
            Отмена
          </button>
          {step < 3 && (
            <button
              className="btn btn-primary btn-sm"
              onClick={step === 1 ? goToStep2 : goToStep3}
            >
              Далее
            </button>
          )}
          {step === 3 && (
            <button
              className="btn btn-primary btn-sm"
              onClick={() => executeMutation.mutate()}
              disabled={executeMutation.isPending}
            >
              {executeMutation.isPending ? 'Запуск...' : 'Запустить'}
            </button>
          )}
        </div>
      </div>
    </Modal>
  )
}

// ---------------------------------------------------------------------------
// Execution detail modal
// ---------------------------------------------------------------------------

interface ExecutionDetailModalProps {
  execution: ScriptExecution
  scriptName: string
  onClose: () => void
  onCancelled: () => void
}

function ExecutionDetailModal({
  execution,
  scriptName,
  onClose,
  onCancelled,
}: ExecutionDetailModalProps) {
  const cancelMutation = useMutation({
    mutationFn: () => scriptsApi.cancelExecution(execution.id),
    onSuccess: () => {
      notifySuccess('Выполнение отменено')
      onCancelled()
    },
    onError: (err: unknown) => {
      const e = err as { response?: { data?: { error?: string; message?: string } }; message?: string }
      notifyError(
        e?.response?.data?.error ||
          e?.response?.data?.message ||
          e?.message ||
          'Не удалось отменить выполнение'
      )
    },
  })

  const canCancel = execution.status === 'pending' || execution.status === 'running'

  return (
    <Modal
      isOpen
      onClose={onClose}
      title="Детали выполнения"
      className="scripts-exec-detail-modal"
      disabled={cancelMutation.isPending}
    >
      <div className="scripts-detail">
        <div className="scripts-detail__header">
          <span className="scripts-detail__name">{scriptName}</span>
          <span
            className={`exec-status ${executionStatusClass(execution.status)}`}
          >
            {executionStatusLabel(execution.status)}
          </span>
        </div>

        <div className="scripts-detail__row">
          <span className="scripts-detail__label">Запросил</span>
          <span className="scripts-detail__value">{execution.requested_by}</span>
        </div>
        <div className="scripts-detail__row">
          <span className="scripts-detail__label">Начало</span>
          <span className="scripts-detail__value">
            {execution.started_at
              ? format(new Date(execution.started_at), 'dd MMM yyyy HH:mm:ss')
              : '—'}
          </span>
        </div>
        <div className="scripts-detail__row">
          <span className="scripts-detail__label">Конец</span>
          <span className="scripts-detail__value">
            {execution.finished_at
              ? format(new Date(execution.finished_at), 'dd MMM yyyy HH:mm:ss')
              : '—'}
          </span>
        </div>
        <div className="scripts-detail__row">
          <span className="scripts-detail__label">Код выхода</span>
          <span className="scripts-detail__value">
            {execution.exit_code !== null ? execution.exit_code : '—'}
          </span>
        </div>

        <div className="scripts-detail__section">
          <span className="scripts-detail__label">Параметры</span>
          <pre className="scripts-detail__pre">
            {JSON.stringify(execution.parameters, null, 2)}
          </pre>
        </div>

        {execution.stdout && (
          <div className="scripts-detail__section">
            <span className="scripts-detail__label">Stdout</span>
            <pre className="scripts-detail__pre">{execution.stdout}</pre>
          </div>
        )}

        {execution.stderr && (
          <div className="scripts-detail__section">
            <span className="scripts-detail__label">Stderr</span>
            <pre className="scripts-detail__pre scripts-detail__pre--error">
              {execution.stderr}
            </pre>
          </div>
        )}

        {execution.error_message && (
          <div className="scripts-detail__section">
            <span className="scripts-detail__label">Ошибка</span>
            <pre className="scripts-detail__pre scripts-detail__pre--error">
              {execution.error_message}
            </pre>
          </div>
        )}

        <div className="modal-footer">
          <button className="btn btn-secondary btn-sm" onClick={onClose}>
            Закрыть
          </button>
          {canCancel && (
            <button
              className="btn btn-danger btn-sm"
              onClick={() => cancelMutation.mutate()}
              disabled={cancelMutation.isPending}
            >
              {cancelMutation.isPending ? 'Отмена...' : 'Отменить выполнение'}
            </button>
          )}
        </div>
      </div>
    </Modal>
  )
}

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

  const toggleActiveMutation = useMutation({
    mutationFn: (script: Script) =>
      scriptsApi.updateScript(script.id, { is_active: !script.is_active }),
    onSuccess: (updated) => {
      queryClient.invalidateQueries({ queryKey: ['scripts'] })
      notifySuccess(updated.is_active ? 'Скрипт активирован' : 'Скрипт деактивирован')
    },
    onError: (err: unknown) => {
      const e = err as { response?: { data?: { error?: string; message?: string } }; message?: string }
      notifyError(
        e?.response?.data?.error ||
          e?.response?.data?.message ||
          e?.message ||
          'Не удалось изменить статус скрипта'
      )
    },
  })

  const deleteMutation = useMutation({
    mutationFn: (id: string) => scriptsApi.deleteScript(id),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['scripts'] })
      notifySuccess('Скрипт удалён')
    },
    onError: (err: unknown) => {
      const e = err as { response?: { data?: { error?: string; message?: string } }; message?: string }
      notifyError(
        e?.response?.data?.error ||
          e?.response?.data?.message ||
          e?.message ||
          'Не удалось удалить скрипт'
      )
    },
  })

  const scripts = data?.scripts ?? []

  return (
    <>
      <div className="scripts-filters card">
        <div className="scripts-filters__fields">
          <div className="form-group">
            <label htmlFor="sf-filter-service">Сервис</label>
            <input
              id="sf-filter-service"
              type="text"
              placeholder="experiment-service"
              value={filterService}
              onChange={(e) => setFilterService(e.target.value)}
            />
          </div>
          <div className="form-group form-group--checkbox">
            <label htmlFor="sf-filter-active">
              <input
                id="sf-filter-active"
                type="checkbox"
                checked={filterActive === true}
                onChange={(e) => setFilterActive(e.target.checked ? true : undefined)}
              />
              Только активные
            </label>
          </div>
        </div>
        <div className="scripts-filters__actions">
          <PermissionGate permission="scripts.manage" system>
            <button
              className="btn btn-primary btn-sm"
              onClick={() => setEditingScript(null)}
            >
              Создать скрипт
            </button>
          </PermissionGate>
        </div>
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
        <div className="scripts-table-wrap card">
          <table className="scripts-table">
            <thead>
              <tr>
                <th>Название</th>
                <th>Описание</th>
                <th>Сервис</th>
                <th>Тип</th>
                <th>Таймаут</th>
                <th>Активен</th>
                <th>Действия</th>
              </tr>
            </thead>
            <tbody>
              {scripts.map((s) => (
                <tr key={s.id}>
                  <td className="scripts-table__name">{s.name}</td>
                  <td className="scripts-table__desc">{s.description ?? '—'}</td>
                  <td>
                    <span className="scripts-table__service">{s.target_service}</span>
                  </td>
                  <td>
                    <span className="scripts-table__type">{s.script_type}</span>
                  </td>
                  <td>{s.timeout_sec} с</td>
                  <td>
                    <span
                      className={`scripts-table__active ${s.is_active ? 'scripts-table__active--yes' : 'scripts-table__active--no'}`}
                    >
                      {s.is_active ? 'Да' : 'Нет'}
                    </span>
                  </td>
                  <td className="scripts-table__actions">
                    <PermissionGate permission="scripts.manage" system>
                      <button
                        className="btn btn-secondary btn-sm"
                        onClick={() => setEditingScript(s)}
                      >
                        Редактировать
                      </button>
                      <button
                        className="btn btn-secondary btn-sm"
                        onClick={() => toggleActiveMutation.mutate(s)}
                        disabled={toggleActiveMutation.isPending}
                      >
                        {s.is_active ? 'Деактивировать' : 'Активировать'}
                      </button>
                      <button
                        className="btn btn-danger btn-sm"
                        onClick={() => {
                          if (confirm(`Удалить скрипт «${s.name}»?`)) {
                            deleteMutation.mutate(s.id)
                          }
                        }}
                        disabled={deleteMutation.isPending}
                      >
                        Удалить
                      </button>
                    </PermissionGate>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
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
      const executions = query.state.data as ScriptExecution[] | undefined
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
      <div className="scripts-filters card">
        <div className="scripts-filters__fields">
          <div className="form-group">
            <label htmlFor="exec-filter-status">Статус</label>
            <select
              id="exec-filter-status"
              value={statusFilter}
              onChange={(e) => setStatusFilter(e.target.value)}
            >
              <option value="">Все</option>
              <option value="pending">Ожидание</option>
              <option value="running">Выполняется</option>
              <option value="completed">Завершён</option>
              <option value="failed">Ошибка</option>
              <option value="cancelled">Отменён</option>
              <option value="timeout">Таймаут</option>
            </select>
          </div>
        </div>
        <div className="scripts-filters__actions">
          <PermissionGate permission="scripts.execute" system>
            <button
              className="btn btn-primary btn-sm"
              onClick={() => setShowExecuteModal(true)}
            >
              Запустить скрипт
            </button>
          </PermissionGate>
        </div>
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
        <div className="scripts-table-wrap card">
          <table className="scripts-table">
            <thead>
              <tr>
                <th>Скрипт</th>
                <th>Статус</th>
                <th>Запросил</th>
                <th>Сервис</th>
                <th>Начало</th>
                <th>Длительность</th>
              </tr>
            </thead>
            <tbody>
              {executions.map((ex) => (
                <tr
                  key={ex.id}
                  className="scripts-table__row"
                  onClick={() => setSelectedExecution(ex)}
                  role="button"
                  tabIndex={0}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter' || e.key === ' ') setSelectedExecution(ex)
                  }}
                >
                  <td className="scripts-table__name">{scriptName(ex.script_id)}</td>
                  <td>
                    <span className={`exec-status ${executionStatusClass(ex.status)}`}>
                      {executionStatusLabel(ex.status)}
                    </span>
                  </td>
                  <td>{ex.requested_by}</td>
                  <td>
                    <span className="scripts-table__service">
                      {ex.target_instance ?? '—'}
                    </span>
                  </td>
                  <td className="scripts-table__time">
                    {ex.started_at
                      ? format(new Date(ex.started_at), 'dd MMM HH:mm:ss')
                      : '—'}
                  </td>
                  <td>{calcDuration(ex.started_at, ex.finished_at)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      {selectedExecution && (
        <ExecutionDetailModal
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
        <ExecuteScriptModal
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
