import { useState } from 'react'
import { z } from 'zod'
import type { ConfigResponse } from '../../types/configs'
import { experimentQosSchema, flatFieldErrors } from '../../schemas/forms'
import { FormGroup, FormActions } from '../../components/common'
import './RateLimitForms.scss'

interface ExperimentQosFormProps {
  config: ConfigResponse | null
  onSave: (value: Record<string, unknown>) => void
  isSaving?: boolean
  errors?: Record<string, string>
}

export function ExperimentQosForm({ config, onSave, isSaving, errors: apiErrors }: ExperimentQosFormProps) {
  const defaultValue = config?.value as Record<string, unknown> || {}

  const [form, setForm] = useState({
    rate_limit_max_requests: defaultValue.rate_limit_max_requests?.toString() || '1000',
    downstream_timeout_seconds: defaultValue.downstream_timeout_seconds?.toString() || '30.0',
  })

  const [fieldErrors, setFieldErrors] = useState<Record<string, string | undefined>>({})

  const handleChange = (field: string, value: string) => {
    setForm((prev) => ({ ...prev, [field]: value }))
    setFieldErrors((prev) => ({ ...prev, [field]: '' }))
  }

  const handleSave = () => {
    try {
      const parsed = experimentQosSchema.parse({
        rate_limit_max_requests: form.rate_limit_max_requests,
        downstream_timeout_seconds: form.downstream_timeout_seconds,
      })
      onSave(parsed)
    } catch (err) {
      if (err instanceof z.ZodError) {
        setFieldErrors(flatFieldErrors(err))
      }
    }
  }

  return (
    <div className="rate-limit-form">
      <div className="rate-limit-form__header">
        <h3>Experiment Service — QoS конфигурация</h3>
        <p className="rate-limit-form__description">
          Rate limiting и таймауты для API запросов
        </p>
      </div>

      <div className="rate-limit-form__content">
        <FormGroup label="Лимит запросов" required>
          <input
            type="number"
            min="1"
            value={form.rate_limit_max_requests}
            onChange={(e) => handleChange('rate_limit_max_requests', e.target.value)}
            aria-invalid={!!fieldErrors.rate_limit_max_requests}
          />
          {fieldErrors.rate_limit_max_requests && (
            <span className="form-error">{fieldErrors.rate_limit_max_requests}</span>
          )}
          {apiErrors?.rate_limit_max_requests && (
            <span className="form-error">{apiErrors.rate_limit_max_requests}</span>
          )}
        </FormGroup>

        <FormGroup label="Таймаут ответа downstream (сек)" required>
          <input
            type="number"
            min="0"
            step="0.1"
            value={form.downstream_timeout_seconds}
            onChange={(e) => handleChange('downstream_timeout_seconds', e.target.value)}
            aria-invalid={!!fieldErrors.downstream_timeout_seconds}
          />
          {fieldErrors.downstream_timeout_seconds && (
            <span className="form-error">{fieldErrors.downstream_timeout_seconds}</span>
          )}
          {apiErrors?.downstream_timeout_seconds && (
            <span className="form-error">{apiErrors.downstream_timeout_seconds}</span>
          )}
        </FormGroup>
      </div>

      <FormActions>
        <button
          className="btn btn-primary"
          onClick={handleSave}
          disabled={isSaving}
        >
          {isSaving ? 'Сохраняю...' : 'Сохранить'}
        </button>
      </FormActions>
    </div>
  )
}
