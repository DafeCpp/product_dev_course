import { useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { useApiMutation } from '../../hooks/useApiMutation'
import { runSensorsApi, sensorsApi } from '../../api/client'
import { Loading } from '../../components/common'

interface RunSensorsPanelProps {
  runId: string
  projectId: string
}

export default function RunSensorsPanel({ runId, projectId }: RunSensorsPanelProps) {
  const [showAdd, setShowAdd] = useState(false)

  const { data: attachedData, isLoading } = useQuery({
    queryKey: ['run-sensors', runId],
    queryFn: () => runSensorsApi.list(runId, { project_id: projectId }),
    enabled: !!runId,
  })

  const { data: allSensorsData } = useQuery({
    queryKey: ['sensors', projectId],
    queryFn: () => sensorsApi.list({ project_id: projectId }),
    enabled: !!projectId,
  })

  const attachMutation = useApiMutation<unknown, string>({
    mutationFn: (sensorId) => runSensorsApi.attach(runId, sensorId, { project_id: projectId }),
    invalidateKeys: [['run-sensors', runId]],
    successMessage: 'Датчик привязан',
    errorFallback: 'Не удалось привязать датчик',
    onSuccess: () => setShowAdd(false),
  })

  const detachMutation = useApiMutation<unknown, string>({
    mutationFn: (sensorId) => runSensorsApi.detach(runId, sensorId, { project_id: projectId }),
    invalidateKeys: [['run-sensors', runId]],
    successMessage: 'Датчик откреплён',
    errorFallback: 'Не удалось открепить датчик',
  })

  const attached = attachedData?.sensors ?? []
  const attachedIds = new Set(attached.map((s) => s.sensor_id))
  const allSensors = allSensorsData?.sensors ?? []
  const available = allSensors.filter((s) => !attachedIds.has(s.id))

  if (isLoading) return <Loading />

  return (
    <div className="run-sensors">
      {attached.length === 0 ? (
        <p className="run-sensors__empty">Датчики не привязаны</p>
      ) : (
        <div className="run-sensors__list">
          {attached.map((rs) => {
            const sensor = allSensors.find((s) => s.id === rs.sensor_id)
            return (
              <div key={rs.sensor_id} className="run-sensors__item">
                <span className="run-sensors__name">{sensor?.name ?? rs.sensor_id}</span>
                <span className="run-sensors__type">{sensor?.type ?? '—'}</span>
                <span className="run-sensors__mode badge">{rs.mode}</span>
                <button
                  className="btn btn-danger btn-sm"
                  onClick={() => detachMutation.mutate(rs.sensor_id)}
                  disabled={detachMutation.isPending}
                >
                  Открепить
                </button>
              </div>
            )
          })}
        </div>
      )}
      {!showAdd ? (
        <button
          className="btn btn-secondary btn-sm"
          onClick={() => setShowAdd(true)}
          disabled={available.length === 0}
          style={{ marginTop: '0.5rem' }}
        >
          + Привязать датчик
        </button>
      ) : (
        <div className="run-sensors__add" style={{ marginTop: '0.5rem', display: 'flex', gap: '0.5rem' }}>
          <select
            defaultValue=""
            onChange={(e) => { if (e.target.value) attachMutation.mutate(e.target.value) }}
            disabled={attachMutation.isPending}
          >
            <option value="">Выберите датчик...</option>
            {available.map((s) => (
              <option key={s.id} value={s.id}>{s.name} ({s.type})</option>
            ))}
          </select>
          <button className="btn btn-secondary btn-sm" onClick={() => setShowAdd(false)}>
            Отмена
          </button>
        </div>
      )}
    </div>
  )
}
