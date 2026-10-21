import { Link } from 'react-router-dom'
import { memo, useCallback, useMemo, useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { runsApi } from '../api/client'
import { StatusBadge, Loading, Error, EmptyState, runStatusMap } from './common'
import { downloadBlob } from '../utils/download'
import { notifyError, notifySuccess } from '../utils/notify'
import Tags from './common/Tags'
import BulkRunTagsModal from './BulkRunTagsModal'
import './RunsList.scss'
import { getApiErrorMessage } from '../utils/apiError'
import { formatSecondsDuration, formatShortDateTime } from '../utils/format'
import type { Run } from '../types'

interface RunsListProps {
  experimentId: string
}

interface RunRowProps {
  run: Run
  checked: boolean
  onToggle: (runId: string, checked: boolean) => void
}

// Memoized so toggling one row's checkbox re-renders only that row.
const RunRow = memo(function RunRow({ run, checked, onToggle }: RunRowProps) {
  return (
    <tr>
      <td className="runs-select-col">
        <input
          type="checkbox"
          aria-label={`select run ${run.id}`}
          checked={checked}
          onChange={(e) => onToggle(run.id, e.target.checked)}
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
          ? formatShortDateTime(run.started_at)
          : '-'}
      </td>
      <td>
        {run.finished_at
          ? formatShortDateTime(run.finished_at)
          : '-'}
      </td>
      <td>{formatSecondsDuration(run.duration_seconds)}</td>
      <td>
        <Link
          to={`/runs/${run.id}`}
          className="btn btn-secondary btn-sm"
        >
          Подробнее
        </Link>
      </td>
    </tr>
  )
})

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

  const toggleRun = useCallback((runId: string, checked: boolean) => {
    setSelected((prev) => ({ ...prev, [runId]: checked }))
  }, [])

  if (isLoading) {
    return <Loading message="Загрузка запусков..." />
  }

  if (error) {
    return <Error message={getApiErrorMessage(error, 'Ошибка загрузки запусков')} />
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
              <RunRow key={run.id} run={run} checked={!!selected[run.id]} onToggle={toggleRun} />
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

