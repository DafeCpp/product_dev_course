import { useState } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { webhooksApi } from '../api/client'
import { format } from 'date-fns'
import type { WebhookSubscription, WebhookDelivery } from '../types'
import { Loading, Error as ErrorComponent, EmptyState } from '../components/common'
import { notifyError, notifySuccess } from '../utils/notify'
import './Webhooks.scss'

const PAGE_SIZE = 20

/** Known event types for the autocomplete hint. */
const KNOWN_EVENT_TYPES = [
  'run.started',
  'run.finished',
  'run.status_changed',
  'run.archived',
  'run.tags_updated',
  'capture_session.created',
  'capture_session.stopped',
]

function Webhooks() {
  const queryClient = useQueryClient()

  // --- Subscriptions ---
  const [showCreateForm, setShowCreateForm] = useState(false)
  const [formUrl, setFormUrl] = useState('')
  const [formEventTypes, setFormEventTypes] = useState('')
  const [formSecret, setFormSecret] = useState('')

  const {
    data: subscriptionsData,
    isLoading: subsLoading,
    error: subsError,
  } = useQuery({
    queryKey: ['webhooks'],
    queryFn: () => webhooksApi.list({ page_size: 100 }),
  })

  const createMutation = useMutation({
    mutationFn: () => {
      const eventTypes = formEventTypes
        .split(',')
        .map((s) => s.trim())
        .filter(Boolean)
      if (!formUrl.trim()) throw new Error('URL обязателен')
      if (eventTypes.length === 0) throw new Error('Укажите хотя бы один тип события')
      return webhooksApi.create({
        target_url: formUrl.trim(),
        event_types: eventTypes,
        secret: formSecret.trim() || undefined,
      })
    },
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['webhooks'] })
      setShowCreateForm(false)
      setFormUrl('')
      setFormEventTypes('')
      setFormSecret('')
      notifySuccess('Webhook создан')
    },
    onError: (err: any) => {
      const msg =
        err?.response?.data?.message ||
        err?.response?.data?.error ||
        err?.message ||
        'Не удалось создать webhook'
      notifyError(msg)
    },
  })

  const deleteMutation = useMutation({
    mutationFn: (id: string) => webhooksApi.delete(id),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['webhooks'] })
      notifySuccess('Webhook удалён')
    },
    onError: (err: any) => {
      const msg =
        err?.response?.data?.message ||
        err?.response?.data?.error ||
        err?.message ||
        'Не удалось удалить webhook'
      notifyError(msg)
    },
  })

  // --- Deliveries ---
  const [deliveryPage, setDeliveryPage] = useState(0)
  const [deliveryStatusFilter, setDeliveryStatusFilter] = useState('')

  const {
    data: deliveriesData,
    isLoading: deliveriesLoading,
    error: deliveriesError,
  } = useQuery({
    queryKey: ['webhook-deliveries', deliveryPage, deliveryStatusFilter],
    queryFn: () =>
      webhooksApi.listDeliveries({
        page: deliveryPage + 1,
        page_size: PAGE_SIZE,
        status: deliveryStatusFilter || undefined,
      }),
  })

  const retryMutation = useMutation({
    mutationFn: (deliveryId: string) => webhooksApi.retryDelivery(deliveryId),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['webhook-deliveries'] })
      notifySuccess('Повторная доставка запрошена')
    },
    onError: (err: any) => {
      const msg =
        err?.response?.data?.message ||
        err?.response?.data?.error ||
        err?.message ||
        'Не удалось повторить доставку'
      notifyError(msg)
    },
  })

  const subscriptions = subscriptionsData?.webhooks || []
  const deliveries = deliveriesData?.deliveries || []
  const deliveriesTotal = deliveriesData?.total ?? 0
  const deliveriesTotalPages = Math.max(1, Math.ceil(deliveriesTotal / PAGE_SIZE))

  function deliveryStatusClass(status: string): string {
    switch (status) {
      case 'delivered':
        return 'delivery-status--delivered'
      case 'failed':
        return 'delivery-status--failed'
      case 'pending':
        return 'delivery-status--pending'
      default:
        return 'delivery-status--retrying'
    }
  }

  return (
    <div className="webhooks-page">
      {/* Subscriptions */}
      <div className="webhooks-subscriptions card">
        <div className="card-header">
          <h3>Webhook-подписки</h3>
          <button
            className="btn btn-primary btn-sm"
            onClick={() => setShowCreateForm((v) => !v)}
          >
            {showCreateForm ? 'Отмена' : 'Создать'}
          </button>
        </div>

        {showCreateForm && (
          <div className="webhook-create-form">
            <div className="form-row">
              <div className="form-group">
                <label htmlFor="wh-url">Target URL</label>
                <input
                  id="wh-url"
                  type="url"
                  placeholder="https://example.com/webhook"
                  value={formUrl}
                  onChange={(e) => setFormUrl(e.target.value)}
                />
              </div>
              <div className="form-group">
                <label htmlFor="wh-events">Типы событий (через запятую)</label>
                <input
                  id="wh-events"
                  type="text"
                  placeholder="run.started, run.finished, capture_session.created"
                  value={formEventTypes}
                  onChange={(e) => setFormEventTypes(e.target.value)}
                />
                <span className="hint">
                  Доступные: {KNOWN_EVENT_TYPES.join(', ')}
                </span>
              </div>
              <div className="form-group">
                <label htmlFor="wh-secret">Secret (опционально)</label>
                <input
                  id="wh-secret"
                  type="text"
                  placeholder="Секретный ключ для подписи"
                  value={formSecret}
                  onChange={(e) => setFormSecret(e.target.value)}
                />
                <span className="hint">
                  Используется для HMAC-подписи payload
                </span>
              </div>
            </div>
            <div className="form-actions">
              <button
                className="btn btn-secondary btn-sm"
                onClick={() => setShowCreateForm(false)}
              >
                Отмена
              </button>
              <button
                className="btn btn-primary btn-sm"
                onClick={() => createMutation.mutate()}
                disabled={createMutation.isPending}
              >
                {createMutation.isPending ? 'Создание...' : 'Создать webhook'}
              </button>
            </div>
          </div>
        )}

        {subsLoading && <Loading message="Загрузка подписок..." />}
        {subsError && (
          <ErrorComponent
            message={
              subsError instanceof Error
                ? subsError.message
                : 'Ошибка загрузки подписок'
            }
          />
        )}

        {!subsLoading && !subsError && subscriptions.length === 0 && (
          <EmptyState message="Webhook-подписок пока нет">
            {!showCreateForm && (
              <button
                className="btn btn-primary btn-sm"
                onClick={() => setShowCreateForm(true)}
              >
                Создать первый webhook
              </button>
            )}
          </EmptyState>
        )}

        {!subsLoading && !subsError && subscriptions.length > 0 && (
          <div className="webhooks-list">
            {subscriptions.map((wh: WebhookSubscription) => (
              <div key={wh.id} className="webhook-card">
                <div className="webhook-card__info">
                  <div className="webhook-card__url">{wh.target_url}</div>
                  <div className="webhook-card__meta">
                    <span
                      className={`webhook-card__status ${wh.is_active ? 'webhook-card__status--active' : 'webhook-card__status--inactive'}`}
                    >
                      {wh.is_active ? 'Активен' : 'Неактивен'}
                    </span>
                    <div className="webhook-card__event-types">
                      {wh.event_types.map((et) => (
                        <span key={et} className="webhook-card__event-badge">
                          {et}
                        </span>
                      ))}
                    </div>
                    {wh.secret && (
                      <span className="webhook-card__secret-indicator">
                        secret: ****
                      </span>
                    )}
                  </div>
                  <div className="webhook-card__id">ID: {wh.id}</div>
                </div>
                <div className="webhook-card__actions">
                  <button
                    className="btn btn-danger btn-sm"
                    onClick={() => {
                      if (confirm('Удалить webhook?')) {
                        deleteMutation.mutate(wh.id)
                      }
                    }}
                    disabled={deleteMutation.isPending}
                  >
                    Удалить
                  </button>
                </div>
              </div>
            ))}
          </div>
        )}
      </div>

      {/* Deliveries */}
      <div className="webhooks-deliveries card">
        <div className="card-header">
          <h3>Доставки ({deliveriesTotal})</h3>
          <div className="filter-controls">
            <label htmlFor="delivery-status-filter">Статус:</label>
            <select
              id="delivery-status-filter"
              value={deliveryStatusFilter}
              onChange={(e) => {
                setDeliveryStatusFilter(e.target.value)
                setDeliveryPage(0)
              }}
            >
              <option value="">Все</option>
              <option value="pending">Pending</option>
              <option value="delivered">Delivered</option>
              <option value="failed">Failed</option>
            </select>
          </div>
        </div>

        {deliveriesLoading && <Loading message="Загрузка доставок..." />}
        {deliveriesError && (
          <ErrorComponent
            message={
              deliveriesError instanceof Error
                ? deliveriesError.message
                : 'Ошибка загрузки доставок'
            }
          />
        )}

        {!deliveriesLoading && !deliveriesError && deliveries.length === 0 && (
          <EmptyState message="Доставок пока нет" />
        )}

        {!deliveriesLoading && !deliveriesError && deliveries.length > 0 && (
          <>
            <div className="deliveries-list">
              <table>
                <thead>
                  <tr>
                    <th>Статус</th>
                    <th>Событие</th>
                    <th>URL</th>
                    <th>Попытки</th>
                    <th>Ошибка</th>
                    <th>Время</th>
                    <th>Действия</th>
                  </tr>
                </thead>
                <tbody>
                  {deliveries.map((d: WebhookDelivery) => (
                    <tr key={d.id}>
                      <td>
                        <span
                          className={`delivery-status ${deliveryStatusClass(d.status)}`}
                        >
                          {d.status}
                        </span>
                      </td>
                      <td>
                        <span className="delivery-event-type">{d.event_type}</span>
                      </td>
                      <td>
                        <span className="delivery-url" title={d.target_url}>
                          {d.target_url}
                        </span>
                      </td>
                      <td>{d.attempt_count}</td>
                      <td>
                        {d.last_error ? (
                          <span className="delivery-error" title={d.last_error}>
                            {d.last_error}
                          </span>
                        ) : (
                          '-'
                        )}
                      </td>
                      <td>
                        {format(new Date(d.created_at), 'dd MMM HH:mm:ss')}
                      </td>
                      <td className="delivery-actions">
                        {d.status === 'failed' && (
                          <button
                            className="btn btn-secondary btn-sm"
                            onClick={() => retryMutation.mutate(d.id)}
                            disabled={retryMutation.isPending}
                          >
                            Retry
                          </button>
                        )}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>

            {deliveriesTotalPages > 1 && (
              <div className="deliveries-pagination">
                <button
                  disabled={deliveryPage === 0}
                  onClick={() => setDeliveryPage((p) => Math.max(0, p - 1))}
                >
                  ← Назад
                </button>
                <span>
                  {deliveryPage + 1} / {deliveriesTotalPages}
                </span>
                <button
                  disabled={deliveryPage + 1 >= deliveriesTotalPages}
                  onClick={() => setDeliveryPage((p) => p + 1)}
                >
                  Вперёд →
                </button>
              </div>
            )}
          </>
        )}
      </div>
    </div>
  )
}

export default Webhooks
