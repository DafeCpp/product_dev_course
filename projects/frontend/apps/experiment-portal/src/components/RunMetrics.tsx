import { useEffect, useMemo, useRef, useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import Plotly from 'plotly.js-dist-min'
import { metricsApi } from '../api/client'
import type { MetricSummaryItem, RunMetric, MetricBucket } from '../types'
import { Loading, EmptyState } from './common'
import './RunMetrics.scss'

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

const RAW_LIMIT = 10_000
const AGG_THRESHOLD = 10_000

function fmt(n: number | undefined): string {
  if (n === undefined || n === null) return '—'
  return Number.isInteger(n) ? String(n) : n.toFixed(4)
}

// ── Summary card ─────────────────────────────────────────────────────────────

function SummaryCard({
  item,
  color,
  selected,
  onToggle,
}: {
  item: MetricSummaryItem
  color: string
  selected: boolean
  onToggle: () => void
}) {
  return (
    <button
      type="button"
      className={`run-metrics__summary-card${selected ? ' run-metrics__summary-card--active' : ''}`}
      onClick={onToggle}
      style={selected ? { borderTopColor: color } : undefined}
      aria-pressed={selected}
    >
      <span className="run-metrics__summary-card-name">{item.name}</span>
      <span className="run-metrics__summary-card-value">{fmt(item.last_value)}</span>
      <div className="run-metrics__summary-card-stats">
        <span>min <strong>{fmt(item.min)}</strong></span>
        <span>avg <strong>{fmt(item.avg)}</strong></span>
        <span>max <strong>{fmt(item.max)}</strong></span>
        <span>step <strong>{item.last_step}</strong></span>
        <span>n <strong>{item.count}</strong></span>
      </div>
    </button>
  )
}

// ── Multi-metric Plotly chart ─────────────────────────────────────────────────

type ChartSeries = {
  name: string
  color: string
  x: number[]
  y: number[]
  mode: 'lines' | 'lines+markers'
}

function MetricsChart({ series }: { series: ChartSeries[] }) {
  const ref = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (!ref.current) return

    const traces = series.map((s) => ({
      x: s.x,
      y: s.y,
      type: 'scatter' as const,
      mode: s.mode,
      name: s.name,
      line: { color: s.color, width: 2 },
      marker: { color: s.color, size: 4 },
      hovertemplate: `<b>${s.name}</b><br>step=%{x}<br>value=%{y:.4f}<extra></extra>`,
    }))

    Plotly.react(
      ref.current,
      traces,
      {
        margin: { t: 12, r: 16, b: 48, l: 60 },
        height: 280,
        paper_bgcolor: 'rgba(0,0,0,0)',
        plot_bgcolor: 'rgba(15,23,42,0.03)',
        showlegend: series.length > 1,
        legend: { orientation: 'h' as const, y: -0.25 },
        xaxis: {
          title: { text: 'step' },
          tickformat: 'd',
          showgrid: true,
          gridcolor: 'rgba(15,23,42,0.10)',
          zeroline: false,
        },
        yaxis: {
          showgrid: true,
          gridcolor: 'rgba(15,23,42,0.10)',
          zeroline: false,
        },
      },
      { responsive: true, displayModeBar: false }
    )

    return () => {
      if (ref.current) Plotly.purge(ref.current)
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [series])

  return <div ref={ref} style={{ width: '100%' }} />
}

// ── Main component ────────────────────────────────────────────────────────────

export default function RunMetrics({ runId }: Props) {
  const [selectedNames, setSelectedNames] = useState<Set<string>>(new Set())

  // 1. Summary — always loaded, drives selector and cards
  const {
    data: summaryData,
    isLoading: summaryLoading,
    error: summaryError,
  } = useQuery({
    queryKey: ['run-metrics-summary', runId],
    queryFn: () => metricsApi.summary(runId),
    enabled: !!runId,
  })

  const summaryItems = summaryData?.items ?? []

  // Auto-select all metrics on first load
  useEffect(() => {
    if (summaryItems.length > 0 && selectedNames.size === 0) {
      setSelectedNames(new Set(summaryItems.map((s) => s.name)))
    }
  }, [summaryItems, selectedNames.size])

  const activeItems = summaryItems.filter((s) => selectedNames.has(s.name))

  // Names param for raw/aggregations queries
  const namesParam = activeItems.map((s) => s.name).join(',')

  // Determine if we need aggregations (based on max count among selected)
  const maxCount = activeItems.reduce((m, s) => Math.max(m, s.count), 0)
  const useAgg = maxCount >= AGG_THRESHOLD

  // 2a. Raw metrics (when data is small enough)
  const {
    data: rawData,
    isLoading: rawLoading,
  } = useQuery({
    queryKey: ['run-metrics-list', runId, namesParam],
    queryFn: () =>
      metricsApi.list(runId, {
        names: namesParam,
        order: 'asc',
        limit: RAW_LIMIT,
      }),
    enabled: !!runId && activeItems.length > 0 && !useAgg,
  })

  // 2b. Aggregated metrics (when data is large)
  const {
    data: aggData,
    isLoading: aggLoading,
  } = useQuery({
    queryKey: ['run-metrics-agg', runId, namesParam],
    queryFn: () =>
      metricsApi.aggregations(runId, {
        names: namesParam,
        bucket_size: Math.max(1, Math.floor(maxCount / 500)),
      }),
    enabled: !!runId && activeItems.length > 0 && useAgg,
  })

  const chartLoading = useAgg ? aggLoading : rawLoading

  // Build chart series from raw or aggregated data
  const chartSeries = useMemo<ChartSeries[]>(() => {
    return activeItems.map((item, idx) => {
      const color = SERIES_COLORS[
        summaryItems.findIndex((s) => s.name === item.name) % SERIES_COLORS.length
      ] ?? SERIES_COLORS[idx % SERIES_COLORS.length]

      if (useAgg && aggData) {
        const buckets: MetricBucket[] = aggData.items.filter((b) => b.name === item.name)
        return {
          name: item.name,
          color,
          x: buckets.map((b) => b.bucket_start),
          y: buckets.map((b) => b.avg),
          mode: 'lines' as const,
        }
      }

      const points: RunMetric[] = (rawData?.items ?? []).filter((p) => p.name === item.name)
      return {
        name: item.name,
        color,
        x: points.map((p) => p.step),
        y: points.map((p) => p.value),
        mode: points.length <= 200 ? ('lines+markers' as const) : ('lines' as const),
      }
    })
  }, [activeItems, summaryItems, useAgg, rawData, aggData])

  const hasChartData = chartSeries.some((s) => s.x.length > 0)

  // ── Toggle metric selection ──────────────────────────────────────────────

  function toggleName(name: string) {
    setSelectedNames((prev) => {
      const next = new Set(prev)
      if (next.has(name)) {
        next.delete(name)
      } else {
        next.add(name)
      }
      return next
    })
  }

  // ── Render ───────────────────────────────────────────────────────────────

  if (summaryLoading) return <Loading message="Загрузка метрик..." />

  if (summaryError) {
    return (
      <p style={{ color: 'var(--color-danger, #e11d48)', fontSize: '0.875rem' }}>
        Не удалось загрузить метрики
      </p>
    )
  }

  if (summaryItems.length === 0) {
    return <EmptyState message="Нет записанных метрик" />
  }

  return (
    <div className="run-metrics">
      {/* Summary cards + selector */}
      <div className="run-metrics__cards" role="group" aria-label="Выбор метрик">
        {summaryItems.map((item, idx) => (
          <SummaryCard
            key={item.name}
            item={item}
            color={SERIES_COLORS[idx % SERIES_COLORS.length]}
            selected={selectedNames.has(item.name)}
            onToggle={() => toggleName(item.name)}
          />
        ))}
      </div>

      {/* Chart */}
      {activeItems.length === 0 ? (
        <EmptyState message="Выберите метрику для отображения" />
      ) : chartLoading ? (
        <Loading message="Загрузка данных..." />
      ) : !hasChartData ? (
        <EmptyState message="Нет точек данных для выбранных метрик" />
      ) : (
        <>
          {useAgg && (
            <p className="run-metrics__agg-notice">
              Агрегированные данные (более {AGG_THRESHOLD.toLocaleString()} шагов)
            </p>
          )}
          <MetricsChart series={chartSeries} />
        </>
      )}
    </div>
  )
}
