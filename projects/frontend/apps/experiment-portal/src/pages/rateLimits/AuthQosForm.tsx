import { useState } from 'react'
import { z } from 'zod'
import type { ConfigResponse } from '../../types/configs'
import { authQosSchema, flatFieldErrors } from '../../schemas/forms'
import { FormGroup, FormActions } from '../../components/common'
import './RateLimitForms.scss'

interface AuthQosFormProps {
  config: ConfigResponse | null
  onSave: (value: Record<string, unknown>) => void
  isSaving?: boolean
  errors?: Record<string, string>
}

export function AuthQosForm({ config, onSave, isSaving, errors: apiErrors }: AuthQosFormProps) {
  const defaultValue = config?.value as Record<string, unknown> || {}

  const [form, setForm] = useState({
    access_token_ttl_sec: defaultValue.access_token_ttl_sec?.toString() || '900',
    refresh_token_ttl_sec: defaultValue.refresh_token_ttl_sec?.toString() || '3600',
  })

  const [fieldErrors, setFieldErrors] = useState<Record<string, string | undefined>>({})

  const handleChange = (field: string, value: string) => {
    setForm((prev) => ({ ...prev, [field]: value }))
    setFieldErrors((prev) => ({ ...prev, [field]: '' }))
  }

  const handleSave = () => {
    try {
      const parsed = authQosSchema.parse({
        access_token_ttl_sec: form.access_token_ttl_sec,
        refresh_token_ttl_sec: form.refresh_token_ttl_sec,
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
        <h3>Auth Service — TTL конфигурация</h3>
        <p className="rate-limit-form__description">
          Настройка времени жизни access и refresh токенов (в секундах)
        </p>
      </div>

      <div className="rate-limit-form__content">
        <FormGroup label="TTL access-токена (сек)" required>
          <input
            type="number"
            min="1"
            value={form.access_token_ttl_sec}
            onChange={(e) => handleChange('access_token_ttl_sec', e.target.value)}
            aria-invalid={!!fieldErrors.access_token_ttl_sec}
          />
          {fieldErrors.access_token_ttl_sec && (
            <span className="form-error">{fieldErrors.access_token_ttl_sec}</span>
          )}
          {apiErrors?.access_token_ttl_sec && (
            <span className="form-error">{apiErrors.access_token_ttl_sec}</span>
          )}
        </FormGroup>

        <FormGroup label="TTL refresh-токена (сек)" required>
          <input
            type="number"
            min="1"
            value={form.refresh_token_ttl_sec}
            onChange={(e) => handleChange('refresh_token_ttl_sec', e.target.value)}
            aria-invalid={!!fieldErrors.refresh_token_ttl_sec}
          />
          {fieldErrors.refresh_token_ttl_sec && (
            <span className="form-error">{fieldErrors.refresh_token_ttl_sec}</span>
          )}
          {apiErrors?.refresh_token_ttl_sec && (
            <span className="form-error">{apiErrors.refresh_token_ttl_sec}</span>
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
