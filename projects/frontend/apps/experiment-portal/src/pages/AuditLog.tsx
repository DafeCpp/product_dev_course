import { useState } from 'react'
import { useSearchParams } from 'react-router-dom'
import { useQuery } from '@tanstack/react-query'
import { format } from 'date-fns'
import { auditApi } from '../api/audit'
import type { AuditEntry } from '../types/permissions'
import { usePermissions } from '../hooks/usePermissions'
import { Loading, Error as ErrorComponent, EmptyState, Pagination } from '../components/common'
import Modal from '../components/Modal'
import { notifyError } from '../utils/notify'
import './AuditLog.scss'

const LIMIT = 50

function AuditLog() {
  const { hasSystemPermission, isLoading: permissionsLoading } = usePermissions()
  const [searchParams, setSearchParams] = useSearchParams()

  const [draftActorId, setDraftActorId] = useState(searchParams.get('actor_id') ?? '')
  const [draftAction, setDraftAction] = useState(searchParams.get('action') ?? '')
  const [draftScopeType, setDraftScopeType] = useState(searchParams.get('scope_type') ?? '')
  const [draftFrom, setDraftFrom] = useState(searchParams.get('from') ?? '')
  const [draftTo, setDraftTo] = useState(searchParams.get('to') ?? '')

  const [page, setPage] = useState(1)
  const [selectedEntry, setSelectedEntry] = useState<AuditEntry | null>(null)

  const filters = {
    actor_id: searchParams.get('actor_id') || undefined,
    action: searchParams.get('action') || undefined,
    scope_type: (searchParams.get('scope_type') || undefined) as 'system' | 'project' | undefined,
    from: searchParams.get('from') || undefined,
    to: searchParams.get('to') || undefined,
  }

  const offset = (page - 1) * LIMIT

  const { data, isLoading, error } = useQuery({
    queryKey: ['audit-log', filters, offset],
    queryFn: async () => {
      try {
        return await auditApi.queryAuditLog({ ...filters, limit: LIMIT, offset })
      } catch (err: any) {
        const msg =
          err?.response?.data?.error ||
          err?.response?.data?.message ||
          err?.message ||
          'Ошибка загрузки аудит-лога'
        notifyError(msg)
        throw err
      }
    },
    staleTime: 10_000,
    refetchOnWindowFocus: false,
    enabled: !permissionsLoading && hasSystemPermission('audit.read'),
  })

  const handleApply = () => {
    const params: Record<string, string> = {}
    if (draftActorId) params.actor_id = draftActorId
    if (draftAction) params.action = draftAction
    if (draftScopeType) params.scope_type = draftScopeType
    if (draftFrom) params.from = draftFrom
    if (draftTo) params.to = draftTo
    setSearchParams(params)
    setPage(1)
  }

  const handleReset = () => {
    setDraftActorId('')
    setDraftAction('')
    setDraftScopeType('')
    setDraftFrom('')
    setDraftTo('')
    setSearchParams({})
    setPage(1)
  }

  if (permissionsLoading) {
    return <Loading message="Проверка прав доступа..." />
  }

  if (!hasSystemPermission('audit.read')) {
    return (
      <div className="audit-log-page">
        <h2 className="audit-log-page__title">Аудит-лог</h2>
        <div className="audit-log-page__no-access">Нет доступа</div>
      </div>
    )
  }

  const entries = data?.entries ?? []
  const total = data?.total ?? 0

  return (
    <div className="audit-log-page">
      <h2 className="audit-log-page__title">Аудит-лог</h2>

      <div className="audit-log-filters card">
        <div className="audit-log-filters__fields">
          <div className="form-group">
            <label htmlFor="al-actor-id">Пользователь (ID)</label>
            <input
              id="al-actor-id"
              type="text"
              placeholder="UUID пользователя"
              value={draftActorId}
              onChange={(e) => setDraftActorId(e.target.value)}
            />
          </div>
          <div className="form-group">
            <label htmlFor="al-action">Действие</label>
            <input
              id="al-action"
              type="text"
              placeholder="user.login, experiment.create"
              value={draftAction}
              onChange={(e) => setDraftAction(e.target.value)}
            />
          </div>
          <div className="form-group">
            <label htmlFor="al-scope-type">Тип области</label>
            <input
              id="al-scope-type"
              type="text"
              placeholder="project, experiment"
              value={draftScopeType}
              onChange={(e) => setDraftScopeType(e.target.value)}
            />
          </div>
          <div className="form-group">
            <label htmlFor="al-from">С</label>
            <input
              id="al-from"
              type="date"
              value={draftFrom}
              onChange={(e) => setDraftFrom(e.target.value)}
            />
          </div>
          <div className="form-group">
            <label htmlFor="al-to">По</label>
            <input
              id="al-to"
              type="date"
              value={draftTo}
              onChange={(e) => setDraftTo(e.target.value)}
            />
          </div>
        </div>
        <div className="audit-log-filters__actions">
          <button className="btn btn-primary btn-sm" onClick={handleApply}>
            Применить
          </button>
          <button className="btn btn-secondary btn-sm" onClick={handleReset}>
            Сбросить
          </button>
        </div>
      </div>

      {isLoading && <Loading message="Загрузка аудит-лога..." />}
      {error && (
        <ErrorComponent
          message={error instanceof Error ? error.message : 'Ошибка загрузки аудит-лога'}
        />
      )}

      {!isLoading && !error && entries.length === 0 && (
        <EmptyState message="Записей аудит-лога не найдено" />
      )}

      {!isLoading && !error && entries.length > 0 && (
        <>
          <div className="audit-log-table-wrap card">
            <table className="audit-log-table">
              <thead>
                <tr>
                  <th>Время</th>
                  <th>Пользователь</th>
                  <th>Действие</th>
                  <th>Область</th>
                  <th>Цель</th>
                  <th>IP</th>
                </tr>
              </thead>
              <tbody>
                {entries.map((entry: AuditEntry) => (
                  <tr
                    key={entry.id}
                    className="audit-log-table__row"
                    onClick={() => setSelectedEntry(entry)}
                    role="button"
                    tabIndex={0}
                    onKeyDown={(e) => {
                      if (e.key === 'Enter' || e.key === ' ') setSelectedEntry(entry)
                    }}
                  >
                    <td className="audit-log-table__time">
                      {format(new Date(entry.created_at), 'dd MMM yyyy HH:mm:ss')}
                    </td>
                    <td>{entry.actor_username}</td>
                    <td>
                      <span className="audit-log-table__action">{entry.action}</span>
                    </td>
                    <td>
                      {entry.scope_type
                        ? `${entry.scope_type}${entry.scope_id ? ` / ${entry.scope_id}` : ''}`
                        : '—'}
                    </td>
                    <td>
                      {entry.target_type
                        ? `${entry.target_type}${entry.target_id ? ` / ${entry.target_id}` : ''}`
                        : '—'}
                    </td>
                    <td>{entry.ip_address ?? '—'}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>

          <Pagination
            currentPage={page}
            totalItems={total}
            pageSize={LIMIT}
            onPageChange={setPage}
          />
        </>
      )}

      {selectedEntry && (
        <Modal
          isOpen={true}
          onClose={() => setSelectedEntry(null)}
          title="Детали записи аудита"
          className="audit-log-detail-modal"
        >
          <div className="audit-log-detail">
            <div className="audit-log-detail__row">
              <span className="audit-log-detail__label">Время</span>
              <span className="audit-log-detail__value">
                {format(new Date(selectedEntry.created_at), 'dd MMM yyyy HH:mm:ss')}
              </span>
            </div>
            <div className="audit-log-detail__row">
              <span className="audit-log-detail__label">Пользователь</span>
              <span className="audit-log-detail__value">
                {selectedEntry.actor_username}{' '}
                <span className="audit-log-detail__sub">({selectedEntry.actor_id})</span>
              </span>
            </div>
            <div className="audit-log-detail__row">
              <span className="audit-log-detail__label">Действие</span>
              <span className="audit-log-detail__value audit-log-table__action">
                {selectedEntry.action}
              </span>
            </div>
            <div className="audit-log-detail__row">
              <span className="audit-log-detail__label">Область</span>
              <span className="audit-log-detail__value">
                {selectedEntry.scope_type
                  ? `${selectedEntry.scope_type}${selectedEntry.scope_id ? ` / ${selectedEntry.scope_id}` : ''}`
                  : '—'}
              </span>
            </div>
            <div className="audit-log-detail__row">
              <span className="audit-log-detail__label">Цель</span>
              <span className="audit-log-detail__value">
                {selectedEntry.target_type
                  ? `${selectedEntry.target_type}${selectedEntry.target_id ? ` / ${selectedEntry.target_id}` : ''}`
                  : '—'}
              </span>
            </div>
            <div className="audit-log-detail__row">
              <span className="audit-log-detail__label">IP</span>
              <span className="audit-log-detail__value">{selectedEntry.ip_address ?? '—'}</span>
            </div>
            <div className="audit-log-detail__details">
              <span className="audit-log-detail__label">Детали</span>
              <pre className="audit-log-detail__json">
                {JSON.stringify(selectedEntry.details, null, 2)}
              </pre>
            </div>
          </div>
        </Modal>
      )}
    </div>
  )
}

export default AuditLog
