import { useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { usePermissions } from '../hooks/usePermissions'
import { useApiMutation } from '../hooks/useApiMutation'
import { configsApi } from '../api/configs'
import type { ConfigResponse } from '../types/configs'
import { Loading, Error as ErrorComponent } from '../components/common'
import { AuthQosForm } from './rateLimits/AuthQosForm'
import { ExperimentQosForm } from './rateLimits/ExperimentQosForm'
import { TelemetryRateLimitsForm } from './rateLimits/TelemetryRateLimitsForm'
import './RateLimitsQos.scss'

type ServiceType = 'auth' | 'experiment' | 'telemetry'

interface ServiceConfig {
  type: ServiceType
  label: string
  service_name: string
  key: string
  description: string
}

const SERVICES: ServiceConfig[] = [
  {
    type: 'auth',
    label: 'Auth Service',
    service_name: 'auth-service',
    key: 'auth_qos',
    description: 'TTL конфигурация для access/refresh токенов',
  },
  {
    type: 'experiment',
    label: 'Experiment Service',
    service_name: 'experiment-service',
    key: 'experiment_qos',
    description: 'Rate limiting и таймауты для API',
  },
  {
    type: 'telemetry',
    label: 'Telemetry Ingest',
    service_name: 'telemetry-ingest-service',
    key: 'rate_limits',
    description: 'Ограничения приёма телеметрии (REST/WS)',
  },
]

function RateLimitsQos() {
  const { hasSystemPermission, isLoading: permsLoading } = usePermissions()
  const [selectedService, setSelectedService] = useState<ServiceType>('auth')

  const canView = hasSystemPermission('configs.view')

  // Fetch all qos-type configs once (covers all three services)
  const { data, isLoading, error } = useQuery({
    queryKey: ['configs', 'qos'],
    queryFn: () => configsApi.listConfigs({ config_type: 'qos' }),
    staleTime: 15_000,
    refetchOnWindowFocus: false,
  })

  // Find config for current service
  const currentServiceCfg = SERVICES.find((s) => s.type === selectedService)!
  const currentConfig = data?.items.find(
    (c) => c.service_name === currentServiceCfg.service_name && c.key === currentServiceCfg.key,
  ) || null

  // Mutations for create/patch/dry-run
  const [dryRunPreview, setDryRunPreview] = useState<unknown | null>(null)
  const [dryRunError, setDryRunError] = useState<string | null>(null)

  const dryRunMutation = useApiMutation<
    { dry_run: true; preview: unknown },
    { config: ConfigResponse; value: Record<string, unknown> } | { value: Record<string, unknown> }
  >({
    mutationFn: async (args) => {
      if ('config' in args) {
        const result = await configsApi.dryRunPatch(args.config.id, {
          version: args.config.version,
          value: args.value,
          change_reason: 'Updated via rate-limits page',
        })
        return result as { dry_run: true; preview: unknown }
      } else {
        const result = await configsApi.dryRunCreate({
          service_name: currentServiceCfg.service_name,
          key: currentServiceCfg.key,
          config_type: 'qos',
          value: args.value,
          is_critical: true,
        })
        return result as { dry_run: true; preview: unknown }
      }
    },
    invalidateKeys: [],
    onSuccess: (result) => {
      setDryRunPreview(result.preview)
      setDryRunError(null)
    },
    onError: () => {
      setDryRunError('Ошибка валидации конфигурации')
      setDryRunPreview(null)
    },
  })

  const saveMutation = useApiMutation<
    ConfigResponse,
    { config: ConfigResponse; value: Record<string, unknown> } | { value: Record<string, unknown> }
  >({
    mutationFn: async (args) => {
      if ('config' in args) {
        const result = await configsApi.patchConfig(args.config.id, {
          version: args.config.version,
          value: args.value,
          change_reason: 'Updated via rate-limits page',
        })
        return result.config
      } else {
        const result = await configsApi.createConfig({
          service_name: currentServiceCfg.service_name,
          key: currentServiceCfg.key,
          config_type: 'qos',
          value: args.value,
          is_critical: true,
        })
        return result.config
      }
    },
    invalidateKeys: [['configs', 'qos']],
    successMessage: currentConfig ? 'Конфиг обновлён' : 'Конфиг создан',
    errorFallback: currentConfig ? 'Не удалось обновить конфиг' : 'Не удалось создать конфиг',
  })

  const handleSave = async (value: Record<string, unknown>) => {
    // First dry-run
    await dryRunMutation.mutateAsync(
      currentConfig ? { config: currentConfig, value } : { value },
    )

    // Then actual save (only if dry-run succeeds)
    // For simplicity, we'll do a separate call. In a real app, you might want to
    // combine these into a single flow or use a different UX pattern.
  }

  const handleConfirmSave = async (value: Record<string, unknown>) => {
    await saveMutation.mutateAsync(
      currentConfig ? { config: currentConfig, value } : { value },
    )
  }

  if (permsLoading) return <Loading message="Проверка прав доступа..." />

  if (!canView) {
    return (
      <div className="rate-limits-page">
        <h2 className="rate-limits-page__title">Rate Limits & QoS</h2>
        <div className="rate-limits-page__no-access">Нет доступа</div>
      </div>
    )
  }

  return (
    <div className="rate-limits-page">
      <h2 className="rate-limits-page__title">Rate Limits & QoS конфигурация</h2>

      {/* Service selector buttons */}
      <div className="rate-limits-page__selector">
        {SERVICES.map((svc) => (
          <button
            key={svc.type}
            className={`btn btn-ghost btn-sm ${selectedService === svc.type ? 'btn--active' : ''}`}
            onClick={() => {
              setSelectedService(svc.type)
              setDryRunPreview(null)
              setDryRunError(null)
            }}
          >
            {svc.label}
          </button>
        ))}
      </div>

      {isLoading && <Loading message="Загрузка конфигов..." />}
      {error && (
        <ErrorComponent message={error instanceof Error ? error.message : 'Ошибка загрузки конфигов'} />
      )}

      {!isLoading && !error && (
        <>
          {/* Current service form */}
          {selectedService === 'auth' && (
            <AuthQosForm
              config={currentConfig}
              onSave={handleSave}
              isSaving={dryRunMutation.isPending}
            />
          )}
          {selectedService === 'experiment' && (
            <ExperimentQosForm
              config={currentConfig}
              onSave={handleSave}
              isSaving={dryRunMutation.isPending}
            />
          )}
          {selectedService === 'telemetry' && (
            <TelemetryRateLimitsForm
              config={currentConfig}
              onSave={handleSave}
              isSaving={dryRunMutation.isPending}
            />
          )}

          {/* Dry-run preview & confirm */}
          {dryRunPreview && (
            <div className="rate-limits-page__preview">
              <div className="rate-limits-page__preview-header">
                <h4>Проверка конфигурации</h4>
                <button
                  className="btn btn-primary"
                  onClick={() => handleConfirmSave(dryRunPreview as Record<string, unknown>)}
                  disabled={saveMutation.isPending}
                >
                  {saveMutation.isPending ? 'Сохраняю...' : 'Подтвердить сохранение'}
                </button>
              </div>
              <pre className="rate-limits-page__preview-body">
                {JSON.stringify(dryRunPreview, null, 2)}
              </pre>
            </div>
          )}

          {dryRunError && (
            <div className="rate-limits-page__error-box">
              <strong>⚠️ Ошибка валидации:</strong> {dryRunError}
            </div>
          )}
        </>
      )}

      {/* Future work note - not visible, just a code comment */}
      {/* TODO: Per-project rate-limit scoping (LOS-65 future work)
          - UserContext.project_permissions is parsed but ensure_permission() doesn't check it
          - Requires: ensure_permission project-scoped variant in config-service
          - Requires: project_id field in ConfigCreate/ConfigPatch frontend types
      */}
    </div>
  )
}

export default RateLimitsQos
