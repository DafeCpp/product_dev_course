import { useState } from 'react'
import { useApiMutation } from '../../hooks/useApiMutation'
import { configsApi } from '../../api/configs'
import type { ConfigResponse, ConfigType, DryRunResponse } from '../../types/configs'
import Modal from '../../components/Modal'
import { notifySuccess } from '../../utils/notify'

interface ConfigFormData {
  service_name: string
  key: string
  config_type: ConfigType
  description: string
  value_raw: string
  is_critical: boolean
  is_sensitive: boolean
  change_reason: string
}

const EMPTY_FORM: ConfigFormData = {
  service_name: '',
  key: '',
  config_type: 'feature_flag',
  description: '',
  value_raw: '{\n  "enabled": true\n}',
  is_critical: false,
  is_sensitive: false,
  change_reason: '',
}

function configToForm(c: ConfigResponse): ConfigFormData {
  return {
    service_name: c.service_name,
    key: c.key,
    config_type: c.config_type,
    description: c.description ?? '',
    value_raw: JSON.stringify(c.value, null, 2),
    is_critical: c.is_critical,
    is_sensitive: c.is_sensitive,
    change_reason: '',
  }
}

export interface ConfigFormModalProps {
  /** null = create mode; ConfigResponse = edit mode (optimistic lock via version). */
  config: ConfigResponse | null
  onClose: () => void
  onSaved: () => void
}

export default function ConfigFormModal({ config, onClose, onSaved }: ConfigFormModalProps) {
  const isEdit = config !== null
  const [form, setForm] = useState<ConfigFormData>(isEdit ? configToForm(config) : EMPTY_FORM)
  const [errors, setErrors] = useState<Partial<Record<keyof ConfigFormData, string>>>({})
  const [preview, setPreview] = useState<DryRunResponse | null>(null)

  const set = <K extends keyof ConfigFormData>(key: K, value: ConfigFormData[K]) => {
    setForm((prev) => ({ ...prev, [key]: value }))
    setErrors((prev) => ({ ...prev, [key]: undefined }))
    setPreview(null)
  }

  const parseValue = (): unknown => JSON.parse(form.value_raw)

  const validate = (): boolean => {
    const next: typeof errors = {}
    if (!isEdit) {
      if (!form.service_name.trim()) next.service_name = 'Обязательное поле'
      if (!form.key.trim()) next.key = 'Обязательное поле'
    }
    try {
      parseValue()
    } catch {
      next.value_raw = 'Невалидный JSON'
    }
    setErrors(next)
    return Object.keys(next).length === 0
  }

  const dryRunMutation = useApiMutation<DryRunResponse, void>({
    mutationFn: async () => {
      const value = parseValue()
      if (isEdit) {
        return configsApi.dryRunPatch(config.id, {
          version: config.version,
          value,
          description: form.description.trim() || undefined,
          is_critical: form.is_critical,
          is_sensitive: form.is_sensitive,
          change_reason: form.change_reason.trim() || undefined,
        })
      }
      return configsApi.dryRunCreate({
        service_name: form.service_name.trim(),
        key: form.key.trim(),
        config_type: form.config_type,
        description: form.description.trim() || undefined,
        value,
        is_critical: form.is_critical,
        is_sensitive: form.is_sensitive,
      })
    },
    errorFallback: 'Dry-run не прошёл (проверьте схему)',
    onSuccess: (data) => setPreview(data),
  })

  const saveMutation = useApiMutation({
    mutationFn: async () => {
      const value = parseValue()
      if (isEdit) {
        return configsApi.patchConfig(config.id, {
          version: config.version,
          value,
          description: form.description.trim() || undefined,
          is_critical: form.is_critical,
          is_sensitive: form.is_sensitive,
          change_reason: form.change_reason.trim() || undefined,
        })
      }
      return configsApi.createConfig({
        service_name: form.service_name.trim(),
        key: form.key.trim(),
        config_type: form.config_type,
        description: form.description.trim() || undefined,
        value,
        is_critical: form.is_critical,
        is_sensitive: form.is_sensitive,
      })
    },
    errorFallback: isEdit
      ? 'Не удалось сохранить (возможен конфликт версий — обновите данные)'
      : 'Не удалось создать конфиг',
    onSuccess: () => {
      notifySuccess(isEdit ? 'Конфиг обновлён' : 'Конфиг создан')
      onSaved()
    },
  })

  const handleDryRun = () => {
    if (validate()) dryRunMutation.mutate()
  }
  const handleSubmit = () => {
    if (validate()) saveMutation.mutate()
  }

  const busy = saveMutation.isPending || dryRunMutation.isPending

  return (
    <Modal
      isOpen
      onClose={onClose}
      title={isEdit ? `Редактировать «${config.key}» (v${config.version})` : 'Создать конфиг'}
      className="configs-form-modal"
      disabled={busy}
    >
      <div className="configs-form">
        {!isEdit && (
          <div className="form-row">
            <div className="form-group">
              <label htmlFor="cf-service">Сервис *</label>
              <input
                id="cf-service"
                type="text"
                placeholder="auth-service"
                value={form.service_name}
                onChange={(e) => set('service_name', e.target.value)}
              />
              {errors.service_name && <small className="field-error">{errors.service_name}</small>}
            </div>
            <div className="form-group">
              <label htmlFor="cf-key">Ключ *</label>
              <input
                id="cf-key"
                type="text"
                placeholder="rate_limits"
                value={form.key}
                onChange={(e) => set('key', e.target.value)}
              />
              {errors.key && <small className="field-error">{errors.key}</small>}
            </div>
            <div className="form-group form-group--narrow">
              <label htmlFor="cf-type">Тип</label>
              <select
                id="cf-type"
                value={form.config_type}
                onChange={(e) => set('config_type', e.target.value as ConfigType)}
              >
                <option value="feature_flag">feature_flag</option>
                <option value="qos">qos</option>
              </select>
            </div>
          </div>
        )}

        <div className="form-group">
          <label htmlFor="cf-description">Описание</label>
          <input
            id="cf-description"
            type="text"
            value={form.description}
            onChange={(e) => set('description', e.target.value)}
          />
        </div>

        <div className="form-group">
          <label htmlFor="cf-value">Значение (JSON)</label>
          <textarea
            id="cf-value"
            rows={8}
            value={form.value_raw}
            onChange={(e) => set('value_raw', e.target.value)}
            className="configs-form__code"
          />
          {errors.value_raw && <small className="field-error">{errors.value_raw}</small>}
        </div>

        <div className="form-row">
          <label className="configs-form__check">
            <input
              type="checkbox"
              checked={form.is_critical}
              onChange={(e) => set('is_critical', e.target.checked)}
            />
            Критичный
          </label>
          <label className="configs-form__check">
            <input
              type="checkbox"
              checked={form.is_sensitive}
              onChange={(e) => set('is_sensitive', e.target.checked)}
            />
            Sensitive
          </label>
        </div>

        {isEdit && (
          <div className="form-group">
            <label htmlFor="cf-reason">Причина изменения</label>
            <input
              id="cf-reason"
              type="text"
              value={form.change_reason}
              onChange={(e) => set('change_reason', e.target.value)}
            />
          </div>
        )}

        {preview && (
          <div className="configs-form__preview" data-testid="dry-run-preview">
            <strong>Dry-run preview</strong>
            <pre>{JSON.stringify(preview.preview, null, 2)}</pre>
          </div>
        )}

        <div className="modal-footer">
          <button className="btn btn-secondary btn-sm" onClick={onClose} disabled={busy}>
            Отмена
          </button>
          <button className="btn btn-ghost btn-sm" onClick={handleDryRun} disabled={busy}>
            {dryRunMutation.isPending ? 'Проверка...' : 'Dry-run'}
          </button>
          <button className="btn btn-primary btn-sm" onClick={handleSubmit} disabled={busy}>
            {saveMutation.isPending ? 'Сохранение...' : isEdit ? 'Сохранить' : 'Создать'}
          </button>
        </div>
      </div>
    </Modal>
  )
}
