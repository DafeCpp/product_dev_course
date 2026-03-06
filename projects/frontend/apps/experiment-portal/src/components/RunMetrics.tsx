import { useEffect, useRef, useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import Plotly from 'plotly.js-dist-min'
import { metricsApi } from '../api/client'
import type { RunMetricSeries } from '../types'
import { Loading, EmptyState } from './common'

type Props = {
  runId: string
}

const SERIES_COLORS = [
  '#2563eb',
  '#16a34a',
  '#f97316',
  '#a855f7',
  '#06b6d4',
  '#e11d48',
  '#ca8a04',
  '#0ea5e9',
]

function MetricChart({ series, color }: { series: RunMetricSeries; color: string }) {
  const ref = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (!ref.current || series.points.length === 0) return
    const x = series.points.map((p) => p.step)
    const y = series.points.map((p) => p.value)
    Plotly.react(
      ref.current,
      [
        {
          x,
          y,
          type: 'scatter',
          mode: 'lines+markers',
          line: { color, width: 2 },
          marker: { color, size: 4 },
          name: series.name,
        },
      ],
      {
        margin: { t: 8, r: 16, b: 40, l: 52 },
        height: 220,
        xaxis: { title: { text: 'step' }, tickformat: 'd' },
        yaxis: { title: { text: series.name } },
        showlegend: false,
      },
      { responsive: true, displayModeBar: false }
    )
    return () => {
      if (ref.current) Plotly.purge(ref.current)
    }
  }, [series, color])

  if (series.points.length === 0) {
    return <EmptyState message={`Нет точек для метрики "${series.name}"`} />
  }

  return <div ref={ref} style={{ width: '100%' }} />
}

export default function RunMetrics({ runId }: Props) {
  const [selectedName, setSelectedName] = useState<string | null>(null)

  const { data, isLoading, error } = useQuery({
    queryKey: ['run-metrics', runId],
    queryFn: () => metricsApi.query(runId),
    enabled: !!runId,
  })

  const series = data?.series ?? []

  useEffect(() => {
    if (series.length > 0 && selectedName === null) {
      setSelectedName(series[0].name)
    }
  }, [series, selectedName])

  if (isLoading) return <Loading message="Загрузка метрик..." />

  if (error) {
    return <p style={{ color: 'var(--color-danger, #e11d48)', fontSize: '0.875rem' }}>Не удалось загрузить метрики</p>
  }

  if (series.length === 0) {
    return <EmptyState message="Метрики не записаны" />
  }

  const activeSeries = series.find((s) => s.name === selectedName) ?? series[0]
  const activeIndex = series.findIndex((s) => s.name === activeSeries.name)
  const color = SERIES_COLORS[activeIndex % SERIES_COLORS.length]

  return (
    <div className="run-metrics">
      {series.length > 1 && (
        <div className="run-metrics__tabs" role="tablist">
          {series.map((s, i) => (
            <button
              key={s.name}
              role="tab"
              aria-selected={s.name === activeSeries.name}
              className={`run-metrics__tab${s.name === activeSeries.name ? ' active' : ''}`}
              onClick={() => setSelectedName(s.name)}
              style={s.name === activeSeries.name ? { borderBottomColor: SERIES_COLORS[i % SERIES_COLORS.length] } : undefined}
            >
              {s.name}
              <span className="run-metrics__tab-count">{s.points.length}</span>
            </button>
          ))}
        </div>
      )}
      <MetricChart key={activeSeries.name} series={activeSeries} color={color} />
      <p className="run-metrics__summary">
        {activeSeries.points.length} точек · последнее значение:{' '}
        <strong>{activeSeries.points.at(-1)?.value ?? '—'}</strong> на шаге{' '}
        <strong>{activeSeries.points.at(-1)?.step ?? '—'}</strong>
      </p>
    </div>
  )
}
