import { useState } from 'react'
import { useApiMutation } from '../../hooks/useApiMutation'
import { scriptsApi } from '../../api/scripts'
import type { Script, ScriptType } from '../../types/scripts'
import Modal from '../../components/Modal'
import { notifySuccess } from '../../utils/notify'

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

export interface ScriptFormModalProps {
  script: Script | null
  onClose: () => void
  onSaved: () => void
}

export default function ScriptFormModal({ script, onClose, onSaved }: ScriptFormModalProps) {
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

  const saveMutation = useApiMutation({
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
    errorFallback: 'Не удалось сохранить скрипт',
    onSuccess: () => { notifySuccess(isEdit ? 'Скрипт обновлён' : 'Скрипт создан'); onSaved() },
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
