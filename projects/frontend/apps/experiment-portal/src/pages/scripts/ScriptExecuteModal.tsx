import { useState } from 'react'
import { useApiMutation } from '../../hooks/useApiMutation'
import { scriptsApi } from '../../api/scripts'
import type { Script } from '../../types/scripts'
import Modal from '../../components/Modal'
import { notifyError } from '../../utils/notify'

export interface ScriptExecuteModalProps {
  scripts: Script[]
  onClose: () => void
  onExecuted: () => void
}

export default function ScriptExecuteModal({ scripts, onClose, onExecuted }: ScriptExecuteModalProps) {
  const [step, setStep] = useState<1 | 2 | 3>(1)
  const [selectedScript, setSelectedScript] = useState<Script | null>(null)
  const [paramsRaw, setParamsRaw] = useState('')
  const [paramsError, setParamsError] = useState('')

  const executeMutation = useApiMutation({
    mutationFn: async () => {
      if (!selectedScript) throw new Error('Скрипт не выбран')
      let parameters: Record<string, unknown> = {}
      if (paramsRaw.trim()) {
        parameters = JSON.parse(paramsRaw) as Record<string, unknown>
      }
      return scriptsApi.executeScript(selectedScript.id, { parameters })
    },
    successMessage: 'Скрипт запущен',
    errorFallback: 'Не удалось запустить скрипт',
    onSuccess: () => onExecuted(),
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
