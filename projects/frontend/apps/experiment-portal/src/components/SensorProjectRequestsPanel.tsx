import { useQuery } from '@tanstack/react-query'
import { format } from 'date-fns'
import { sensorsApi } from '../api/client'
import { useApiMutation } from '../hooks/useApiMutation'
import type { SensorProjectRequest } from '../types'
import './SensorProjectRequestsPanel.scss'

interface SensorProjectRequestsPanelProps {
  projectId: string
  /** Project names by id, to show where a request comes from. */
  projectNames?: Record<string, string>
}

type Decision = { request: SensorProjectRequest; action: 'approve' | 'reject' | 'cancel' }

/**
 * Pending requests to attach sensors: incoming ones (for this project's sensors,
 * approved or rejected here) and outgoing ones (made by this project, cancellable).
 */
function SensorProjectRequestsPanel({ projectId, projectNames = {} }: SensorProjectRequestsPanelProps) {
  const incomingKey = ['sensor-project-requests', projectId, 'incoming']
  const outgoingKey = ['sensor-project-requests', projectId, 'outgoing']

  const { data: incoming } = useQuery({
    queryKey: incomingKey,
    queryFn: () =>
      sensorsApi.listProjectRequests({ project_id: projectId, direction: 'incoming', status: 'pending' }),
  })
  const { data: outgoing } = useQuery({
    queryKey: outgoingKey,
    queryFn: () =>
      sensorsApi.listProjectRequests({ project_id: projectId, direction: 'outgoing', status: 'pending' }),
  })

  const decideMutation = useApiMutation<SensorProjectRequest, Decision>({
    mutationFn: ({ request, action }) =>
      action === 'cancel'
        ? sensorsApi.cancelProjectRequest(request.id, projectId)
        : sensorsApi.decideProjectRequest(request.id, action, projectId),
    invalidateKeys: [incomingKey, outgoingKey, ['sensors']],
    successMessage: 'Запрос обработан',
    errorFallback: 'Не удалось обработать запрос',
  })

  const incomingItems = incoming?.requests ?? []
  const outgoingItems = outgoing?.requests ?? []
  if (incomingItems.length === 0 && outgoingItems.length === 0) return null

  const projectLabel = (id: string) => projectNames[id] || id
  const busy = decideMutation.isPending

  return (
    <div className="sensor-requests card" aria-label="Запросы на привязку датчиков">
      {incomingItems.length > 0 && (
        <section>
          <h3 className="sensor-requests__title">Запросы на привязку ваших датчиков</h3>
          <ul className="sensor-requests__list">
            {incomingItems.map((request) => (
              <li key={request.id} className="sensor-requests__item">
                <span>
                  <strong>{request.sensor_name || request.sensor_id}</strong> в проект{' '}
                  <strong>{projectLabel(request.project_id)}</strong>
                  <span className="text-muted"> · {format(new Date(request.created_at), 'dd MMM yyyy HH:mm')}</span>
                </span>
                <span className="sensor-requests__actions">
                  <button
                    type="button"
                    className="btn btn-primary btn-sm"
                    disabled={busy}
                    onClick={() => decideMutation.mutate({ request, action: 'approve' })}
                  >
                    Разрешить
                  </button>
                  <button
                    type="button"
                    className="btn btn-secondary btn-sm"
                    disabled={busy}
                    onClick={() => decideMutation.mutate({ request, action: 'reject' })}
                  >
                    Отклонить
                  </button>
                </span>
              </li>
            ))}
          </ul>
        </section>
      )}
      {outgoingItems.length > 0 && (
        <section>
          <h3 className="sensor-requests__title">Ожидают подтверждения владельца датчика</h3>
          <ul className="sensor-requests__list">
            {outgoingItems.map((request) => (
              <li key={request.id} className="sensor-requests__item">
                <span>
                  Датчик <code>{request.sensor_id}</code>
                  <span className="text-muted"> · {format(new Date(request.created_at), 'dd MMM yyyy HH:mm')}</span>
                </span>
                <span className="sensor-requests__actions">
                  <button
                    type="button"
                    className="btn btn-secondary btn-sm"
                    disabled={busy}
                    onClick={() => decideMutation.mutate({ request, action: 'cancel' })}
                  >
                    Отозвать
                  </button>
                </span>
              </li>
            ))}
          </ul>
        </section>
      )}
    </div>
  )
}

export default SensorProjectRequestsPanel
