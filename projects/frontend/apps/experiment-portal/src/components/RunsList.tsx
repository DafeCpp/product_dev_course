import { Link } from 'react-router-dom'
import { useMemo, useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { runsApi } from '../api/client'
import { format } from 'date-fns'
import { StatusBadge, Loading, Error, EmptyState, runStatusMap } from './common'
import { IS_TEST } from '../utils/env'
import { downloadBlob } from '../utils/download'
import { notifyError, notifySuccess } from '../utils/notify'
import Tags from './common/Tags'
import BulkRunTagsModal from './BulkRunTagsModal'
import './RunsList.scss'

interface RunsListProps {
  experimentId: string
}

function RunsList({ experimentId }: RunsListProps) {
  const { data, isLoading, error } = useQuery({
    queryKey: ['runs', experimentId],
    queryFn: () => runsApi.list(experimentId),
  })

  const [selected, setSelected] = useState<Record<string, boolean>>({})
  const [isBulkOpen, setIsBulkOpen] = useState(false)
  const [exporting, setExporting] = useState(false)

  const handleExport = async (fmt: 'csv' | 'json') => {
    setExporting(true)
    try {
      const data = await runsApi.exportData(experimentId, { format: fmt })
      const ext = fmt === 'csv' ? 'csv' : 'json'
      const mime = fmt === 'csv' ? 'text/csv' : 'application/json'
      downloadBlob(data, `runs.${ext}`, mime)
      notifySuccess(`${ext.toUpperCase()} экспортирован`)
    } catch {
      notifyError(`Ошибка экспорта ${fmt.toUpperCase()}`)
    } finally {
      setExporting(false)
    }
  }

  const runs = data?.runs ?? []
  const selectedRunIds = useMemo(
    () => runs.map((r) => r.id).filter((id) => selected[id]),
    [runs, selected]
  )
  const allSelected = runs.length > 0 && selectedRunIds.length === runs.length

  const formatDuration = (seconds?: number) => {
    if (!seconds) return '-'
    const hours = Math.floor(seconds / 3600)
    const minutes = Math.floor((seconds % 3600) / 60)
    const secs = seconds % 60
    if (hours > 0) {
      return `${hours}ч ${minutes}м ${secs}с`
    }
    if (minutes > 0) {
      return `${minutes}м ${secs}с`
    }
    return `${secs}с`
  }

  if (isLoading) {
    return <Loading message="Загрузка запусков..." />
  }

  if (error) {
    return IS_TEST ? <Error message="Ошибка загрузки запусков" /> : <EmptyState message="Запуски недоступны" />
  }

  if (!data || runs.length === 0) {
    return <EmptyState message="Запуски не найдены" />
  }

  return (
    <div className="runs-list">
      <div className="runs-actions">
        <button
          type="button"
          className="btn btn-secondary"
          onClick={() => setIsBulkOpen(true)}
          disabled={selectedRunIds.length === 0}
        >
          Bulk tagging ({selectedRunIds.length})
        </button>
        <button
          type="button"
          className="btn btn-secondary btn-sm"
          disabled={exporting}
          onClick={() => handleExport('csv')}
        >
          {exporting ? 'Экспорт...' : 'Экспорт CSV'}
        </button>
        <button
          type="button"
          className="btn btn-secondary btn-sm"
          disabled={exporting}
          onClick={() => handleExport('json')}
        >
          Экспорт JSON
        </button>
      </div>
      <div className="runs-table">
        <table>
          <thead>
            <tr>
              <th className="runs-select-col">
                <input
                  type="checkbox"
                  aria-label="select all runs"
                  checked={allSelected}
                  onChange={(e) => {
                    const next: Record<string, boolean> = {}
                    if (e.target.checked) {
                      for (const r of runs) next[r.id] = true
                    }
                    setSelected(next)
                  }}
                />
              </th>
              <th>Название</th>
              <th>Статус</th>
              <th>Теги</th>
              <th>Параметры</th>
              <th>Начало</th>
              <th>Завершение</th>
              <th>Длительность</th>
              <th>Действия</th>
            </tr>
          </thead>
          <tbody>
            {runs.map((run) => (
              <tr key={run.id}>
                <td className="runs-select-col">
                  <input
                    type="checkbox"
                    aria-label={`select run ${run.id}`}
                    checked={!!selected[run.id]}
                    onChange={(e) => {
                      setSelected((prev) => ({ ...prev, [run.id]: e.target.checked }))
                    }}
                  />
                </td>
                <td>
                  <Link to={`/runs/${run.id}`} className="run-link">
                    {run.name}
                  </Link>
                </td>
                <td>
                  <StatusBadge status={run.status} statusMap={runStatusMap} />
                </td>
                <td>
                  <Tags tags={run.tags || []} />
                </td>
                <td>
                  <div className="parameters-preview">
                    {Object.keys(run.params).slice(0, 3).map((key) => (
                      <span key={key} className="param-item">
                        {key}: {String(run.params[key]).substring(0, 20)}
                      </span>
                    ))}
                    {Object.keys(run.params).length > 3 && (
                      <span className="param-more">
                        +{Object.keys(run.params).length - 3}
                      </span>
                    )}
                  </div>
                </td>
                <td>
                  {run.started_at
                    ? format(new Date(run.started_at), 'dd MMM HH:mm')
                    : '-'}
                </td>
                <td>
                  {run.finished_at
                    ? format(new Date(run.finished_at), 'dd MMM HH:mm')
                    : '-'}
                </td>
                <td>{formatDuration(run.duration_seconds)}</td>
                <td>
                  <Link
                    to={`/runs/${run.id}`}
                    className="btn btn-secondary btn-sm"
                  >
                    Подробнее
                  </Link>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <BulkRunTagsModal
        isOpen={isBulkOpen}
        onClose={() => setIsBulkOpen(false)}
        experimentId={experimentId}
        runIds={selectedRunIds}
      />
    </div>
  )
}

export default RunsList

