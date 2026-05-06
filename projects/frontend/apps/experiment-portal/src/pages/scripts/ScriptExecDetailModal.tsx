import { format } from 'date-fns'
import { useApiMutation } from '../../hooks/useApiMutation'
import { scriptsApi } from '../../api/scripts'
import type { ScriptExecution } from '../../types/scripts'
import Modal from '../../components/Modal'
import { executionStatusClass, executionStatusLabel } from './utils'

export interface ScriptExecDetailModalProps {
  execution: ScriptExecution
  scriptName: string
  onClose: () => void
  onCancelled: () => void
}

export default function ScriptExecDetailModal({
  execution,
  scriptName,
  onClose,
  onCancelled,
}: ScriptExecDetailModalProps) {
  const cancelMutation = useApiMutation({
    mutationFn: () => scriptsApi.cancelExecution(execution.id),
    successMessage: 'Выполнение отменено',
    errorFallback: 'Не удалось отменить выполнение',
    onSuccess: () => onCancelled(),
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
