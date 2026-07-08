import { useState } from 'react'
import { z } from 'zod'
import type { ConfigResponse } from '../../types/configs'
import { telemetryRateLimitsSchema, flatFieldErrors } from '../../schemas/forms'
import { FormGroup, FormActions } from '../../components/common'
import './RateLimitForms.scss'

interface RestLimits {
  max_requests: number
  max_readings: number
  window_seconds: number
}

interface WsLimits {
  max_messages: number
  max_readings: number
  window_seconds: number
}

interface TelemetryRateLimitsFormProps {
  config: ConfigResponse | null
  onSave: (value: Record<string, unknown>) => void
  isSaving?: boolean
  errors?: Record<string, string>
}

export function TelemetryRateLimitsForm({
  config,
  onSave,
  isSaving,
}: TelemetryRateLimitsFormProps) {
  const defaultValue = config?.value as Record<string, unknown> || {}
  const defaultRest = (defaultValue.rest as RestLimits) || {
    max_requests: 600,
    max_readings: 60000,
    window_seconds: 60.0,
  }
  const defaultWs = (defaultValue.ws as WsLimits) || {
    max_messages: 600,
    max_readings: 60000,
    window_seconds: 1.0,
  }

  const [form, setForm] = useState({
    rest_max_requests: defaultRest.max_requests.toString(),
    rest_max_readings: defaultRest.max_readings.toString(),
    rest_window_seconds: defaultRest.window_seconds.toString(),
    ws_max_messages: defaultWs.max_messages.toString(),
    ws_max_readings: defaultWs.max_readings.toString(),
    ws_window_seconds: defaultWs.window_seconds.toString(),
    spool_flush_timeout_seconds: (defaultValue.spool_flush_timeout_seconds || 5.0).toString(),
    ws_max_message_bytes: (defaultValue.ws_max_message_bytes || 1048576).toString(),
  })

  const [fieldErrors, setFieldErrors] = useState<Record<string, string>>({})

  const handleChange = (field: string, value: string) => {
    setForm((prev) => ({ ...prev, [field]: value }))
    setFieldErrors((prev) => ({ ...prev, [field]: '' }))
  }

  const handleSave = () => {
    try {
      const parsed = telemetryRateLimitsSchema.parse({
        rest: {
          max_requests: form.rest_max_requests,
          max_readings: form.rest_max_readings,
          window_seconds: form.rest_window_seconds,
        },
        ws: {
          max_messages: form.ws_max_messages,
          max_readings: form.ws_max_readings,
          window_seconds: form.ws_window_seconds,
        },
        spool_flush_timeout_seconds: form.spool_flush_timeout_seconds,
        ws_max_message_bytes: form.ws_max_message_bytes,
      })
      onSave(parsed)
    } catch (err) {
      if (err instanceof z.ZodError) {
        const errors = flatFieldErrors(err)
        // Flatten nested errors from zod
        const flatErrors: Record<string, string> = {}
        for (const [key, value] of Object.entries(errors)) {
          if (key.includes('.')) {
            const [prefix, suffix] = key.split('.')
            flatErrors[`${prefix}_${suffix}`] = value || ''
          } else {
            flatErrors[key] = value || ''
          }
        }
        setFieldErrors(flatErrors)
      }
    }
  }

  return (
    <div className="rate-limit-form">
      <div className="rate-limit-form__header">
        <h3>Telemetry Ingest Service — Rate Limit конфигурация</h3>
        <p className="rate-limit-form__description">
          Ограничения для REST и WebSocket приёма телеметрии, плюс настройки spool и message-size
        </p>
      </div>

      <div className="rate-limit-form__content">
        <div className="rate-limit-form__section">
          <h4 className="rate-limit-form__section-title">REST API</h4>

          <FormGroup label="Макс. запросов / окно" required>
            <input
              type="number"
              min="0"
              value={form.rest_max_requests}
              onChange={(e) => handleChange('rest_max_requests', e.target.value)}
              aria-invalid={!!fieldErrors.rest_max_requests}
            />
            <small>0 = неограниченно</small>
            {fieldErrors.rest_max_requests && (
              <span className="form-error">{fieldErrors.rest_max_requests}</span>
            )}
          </FormGroup>

          <FormGroup label="Макс. показаний / окно" required>
            <input
              type="number"
              min="0"
              value={form.rest_max_readings}
              onChange={(e) => handleChange('rest_max_readings', e.target.value)}
              aria-invalid={!!fieldErrors.rest_max_readings}
            />
            <small>0 = неограниченно</small>
            {fieldErrors.rest_max_readings && (
              <span className="form-error">{fieldErrors.rest_max_readings}</span>
            )}
          </FormGroup>

          <FormGroup label="Окно (сек)" required>
            <input
              type="number"
              min="0.1"
              step="0.1"
              value={form.rest_window_seconds}
              onChange={(e) => handleChange('rest_window_seconds', e.target.value)}
              aria-invalid={!!fieldErrors.rest_window_seconds}
            />
            {fieldErrors.rest_window_seconds && (
              <span className="form-error">{fieldErrors.rest_window_seconds}</span>
            )}
          </FormGroup>
        </div>

        <div className="rate-limit-form__section">
          <h4 className="rate-limit-form__section-title">WebSocket</h4>

          <FormGroup label="Макс. сообщений / окно" required>
            <input
              type="number"
              min="0"
              value={form.ws_max_messages}
              onChange={(e) => handleChange('ws_max_messages', e.target.value)}
              aria-invalid={!!fieldErrors.ws_max_messages}
            />
            <small>0 = неограниченно</small>
            {fieldErrors.ws_max_messages && (
              <span className="form-error">{fieldErrors.ws_max_messages}</span>
            )}
          </FormGroup>

          <FormGroup label="Макс. показаний / окно" required>
            <input
              type="number"
              min="0"
              value={form.ws_max_readings}
              onChange={(e) => handleChange('ws_max_readings', e.target.value)}
              aria-invalid={!!fieldErrors.ws_max_readings}
            />
            <small>0 = неограниченно</small>
            {fieldErrors.ws_max_readings && (
              <span className="form-error">{fieldErrors.ws_max_readings}</span>
            )}
          </FormGroup>

          <FormGroup label="Окно (сек)" required>
            <input
              type="number"
              min="0.1"
              step="0.1"
              value={form.ws_window_seconds}
              onChange={(e) => handleChange('ws_window_seconds', e.target.value)}
              aria-invalid={!!fieldErrors.ws_window_seconds}
            />
            {fieldErrors.ws_window_seconds && (
              <span className="form-error">{fieldErrors.ws_window_seconds}</span>
            )}
          </FormGroup>

          <FormGroup label="Макс. размер сообщения (байт)" required>
            <input
              type="number"
              min="1"
              value={form.ws_max_message_bytes}
              onChange={(e) => handleChange('ws_max_message_bytes', e.target.value)}
              aria-invalid={!!fieldErrors.ws_max_message_bytes}
            />
            {fieldErrors.ws_max_message_bytes && (
              <span className="form-error">{fieldErrors.ws_max_message_bytes}</span>
            )}
          </FormGroup>
        </div>

        <div className="rate-limit-form__section">
          <h4 className="rate-limit-form__section-title">Spool & Misc</h4>

          <FormGroup label="Таймаут сброса spool (сек)" required>
            <input
              type="number"
              min="0.1"
              step="0.1"
              value={form.spool_flush_timeout_seconds}
              onChange={(e) => handleChange('spool_flush_timeout_seconds', e.target.value)}
              aria-invalid={!!fieldErrors.spool_flush_timeout_seconds}
            />
            {fieldErrors.spool_flush_timeout_seconds && (
              <span className="form-error">{fieldErrors.spool_flush_timeout_seconds}</span>
            )}
          </FormGroup>
        </div>
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
