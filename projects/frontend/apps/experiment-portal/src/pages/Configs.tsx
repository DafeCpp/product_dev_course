import { useState } from 'react'
import { useQuery, useQueryClient } from '@tanstack/react-query'
import { useApiMutation } from '../hooks/useApiMutation'
import { configsApi } from '../api/configs'
import type { ConfigResponse, ConfigType } from '../types/configs'
import { usePermissions } from '../hooks/usePermissions'
import {
  Loading,
  Error as ErrorComponent,
  EmptyState,
  MaterialSelect,
  LiveSwitch,
  ListSearchIcon,
} from '../components/common'
import { notifySuccess, notifyError } from '../utils/notify'
import ConfigFormModal from './configs/ConfigFormModal'
import ConfigHistoryModal from './configs/ConfigHistoryModal'
import './Configs.scss'

function Configs() {
  const queryClient = useQueryClient()
  const { hasSystemPermission, isLoading: permsLoading } = usePermissions()

  const [filterService, setFilterService] = useState('')
  const [filterType, setFilterType] = useState<ConfigType | ''>('')
  const [filterActive, setFilterActive] = useState<boolean | undefined>(undefined)
  // undefined = modal closed, null = create, ConfigResponse = edit
  const [editing, setEditing] = useState<ConfigResponse | null | undefined>(undefined)
  const [historyFor, setHistoryFor] = useState<ConfigResponse | null>(null)

  const canCreate = hasSystemPermission('configs.create')
  const canUpdate = hasSystemPermission('configs.update')
  const canDelete = hasSystemPermission('configs.delete')
  const canActivate = hasSystemPermission('configs.activate')
  const canRollback = hasSystemPermission('configs.rollback')

  const filters = {
    service: filterService || undefined,
    config_type: filterType || undefined,
    is_active: filterActive,
  }

  const { data, isLoading, error } = useQuery({
    queryKey: ['configs', filters],
    queryFn: () => configsApi.listConfigs(filters),
    staleTime: 15_000,
    refetchOnWindowFocus: false,
  })

  const toggleActiveMutation = useApiMutation<ConfigResponse, ConfigResponse>({
    mutationFn: (c) =>
      c.is_active
        ? configsApi.deactivate(c.id, { version: c.version })
        : configsApi.activate(c.id, { version: c.version }),
    invalidateKeys: [['configs']],
    errorFallback: 'Не удалось изменить статус (возможен конфликт версий)',
    onSuccess: (updated) =>
      notifySuccess(updated.is_active ? 'Конфиг активирован' : 'Конфиг деактивирован'),
  })

  const deleteMutation = useApiMutation<void, ConfigResponse>({
    mutationFn: (c) =>
      configsApi.deleteConfig(c.id, { version: c.version, changeReason: 'deleted via portal' }),
    invalidateKeys: [['configs']],
    successMessage: 'Конфиг удалён',
    errorFallback: 'Не удалось удалить конфиг',
  })

  const rollbackMutation = useApiMutation<ConfigResponse, { c: ConfigResponse; target: number }>({
    mutationFn: ({ c, target }) =>
      configsApi.rollback(c.id, {
        version: c.version,
        targetVersion: target,
        changeReason: `rollback to v${target} via portal`,
      }),
    invalidateKeys: [['configs']],
    successMessage: 'Откат выполнен',
    errorFallback: 'Не удалось откатить конфиг',
  })

  const configs = data?.items ?? []

  if (permsLoading) return <Loading message="Проверка прав доступа..." />

  if (!hasSystemPermission('configs.view')) {
    return (
      <div className="configs-page">
        <h2 className="configs-page__title">Конфиги</h2>
        <div className="configs-page__no-access">Нет доступа</div>
      </div>
    )
  }

  const handleRollback = (c: ConfigResponse) => {
    const raw = prompt(`Откатить «${c.key}» (текущая v${c.version}) к версии:`)
    if (raw === null) return
    const target = Number(raw)
    if (!Number.isInteger(target) || target < 1 || target >= c.version) {
      notifyError(`Некорректная целевая версия: ${raw}`)
      return
    }
    rollbackMutation.mutate({ c, target })
  }

  return (
    <div className="configs-page">
      <h2 className="configs-page__title">Конфиги</h2>

      <div className="filter-capsule configs-filter-capsule">
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
        <MaterialSelect
          id="config-filter-type"
          label="Тип"
          value={filterType}
          onChange={(v) => setFilterType(v as ConfigType | '')}
          variant="pill"
        >
          <option value="">Все типы</option>
          <option value="feature_flag">feature_flag</option>
          <option value="qos">qos</option>
        </MaterialSelect>
        <LiveSwitch
          live={filterActive === true}
          onChange={(on) => setFilterActive(on ? true : undefined)}
          labelOn="Активные"
          labelOff="Все"
        />
        {canCreate && (
          <button
            className="btn btn-primary btn-sm filter-capsule__btn"
            onClick={() => setEditing(null)}
          >
            + Создать
          </button>
        )}
      </div>

      {isLoading && <Loading message="Загрузка конфигов..." />}
      {error && (
        <ErrorComponent message={error instanceof Error ? error.message : 'Ошибка загрузки конфигов'} />
      )}
      {!isLoading && !error && configs.length === 0 && <EmptyState message="Конфигов не найдено" />}

      {!isLoading && !error && configs.length > 0 && (
        <table className="configs-table">
          <thead>
            <tr>
              <th>Ключ</th>
              <th>Сервис</th>
              <th>Тип</th>
              <th>Версия</th>
              <th>Статус</th>
              <th className="configs-table__actions-col">Действия</th>
            </tr>
          </thead>
          <tbody>
            {configs.map((c) => (
              <tr key={c.id}>
                <td>
                  {c.key}
                  {c.is_sensitive && <span className="configs-badge configs-badge--sensitive">sensitive</span>}
                  {c.is_critical && <span className="configs-badge configs-badge--critical">critical</span>}
                </td>
                <td>{c.service_name}</td>
                <td>{c.config_type}</td>
                <td>v{c.version}</td>
                <td>
                  <span className={`configs-badge ${c.is_active ? 'configs-badge--active' : 'configs-badge--inactive'}`}>
                    {c.is_active ? 'active' : 'inactive'}
                  </span>
                </td>
                <td className="configs-table__actions">
                  <button className="btn btn-ghost btn-xs" onClick={() => setHistoryFor(c)}>
                    История
                  </button>
                  {canUpdate && (
                    <button className="btn btn-ghost btn-xs" onClick={() => setEditing(c)}>
                      Редактировать
                    </button>
                  )}
                  {canActivate && (
                    <button
                      className="btn btn-ghost btn-xs"
                      disabled={toggleActiveMutation.isPending}
                      onClick={() => toggleActiveMutation.mutate(c)}
                    >
                      {c.is_active ? 'Деактивировать' : 'Активировать'}
                    </button>
                  )}
                  {canRollback && c.version > 1 && (
                    <button
                      className="btn btn-ghost btn-xs"
                      disabled={rollbackMutation.isPending}
                      onClick={() => handleRollback(c)}
                    >
                      Откат
                    </button>
                  )}
                  {canDelete && (
                    <button
                      className="btn btn-ghost btn-xs configs-table__danger"
                      disabled={deleteMutation.isPending}
                      onClick={() => {
                        if (confirm(`Удалить конфиг «${c.key}»?`)) deleteMutation.mutate(c)
                      }}
                    >
                      Удалить
                    </button>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}

      {editing !== undefined && (
        <ConfigFormModal
          config={editing}
          onClose={() => setEditing(undefined)}
          onSaved={() => {
            setEditing(undefined)
            queryClient.invalidateQueries({ queryKey: ['configs'] })
          }}
        />
      )}

      {historyFor && (
        <ConfigHistoryModal config={historyFor} onClose={() => setHistoryFor(null)} />
      )}
    </div>
  )
}

export default Configs
