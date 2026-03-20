import { useEffect, useMemo, useRef, useState } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { useQuery, useMutation } from '@tanstack/react-query'
import {
  Box,
  Button,
  Checkbox,
  Chip,
  CircularProgress,
  FormControl,
  FormControlLabel,
  InputLabel,
  MenuItem,
  OutlinedInput,
  Paper,
  Select,
  Table,
  TableBody,
  TableCell,
  TableContainer,
  TableHead,
  TableRow,
  Typography,
  Alert,
} from '@mui/material'
import type { SelectChangeEvent } from '@mui/material'
import Plotly from 'plotly.js-dist-min'
import { runsApi, comparisonApi } from '../api/client'
import type { ComparisonResponse, ComparisonRunEntry } from '../types'

// Standard Plotly palette colours
const PLOTLY_COLORS = [
  '#636EFA',
  '#EF553B',
  '#00CC96',
  '#AB63FA',
  '#FFA15A',
  '#19D3F3',
  '#FF6692',
  '#B6E880',
  '#FF97FF',
  '#FECB52',
]

function fmt(n: number | null | undefined): string {
  if (n === undefined || n === null) return '—'
  return Number.isInteger(n) ? String(n) : n.toFixed(4)
}

// ── Overlay chart for one metric ────────────────────────────────────────────

function OverlayChart({
  metricName,
  runs,
  colorMap,
}: {
  metricName: string
  runs: ComparisonRunEntry[]
  colorMap: Map<string, string>
}) {
  const ref = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (!ref.current) return

    const traces = runs
      .filter((r) => r.metrics[metricName]?.series?.length)
      .map((r) => {
        const series = r.metrics[metricName].series
        const color = colorMap.get(r.run_id) ?? PLOTLY_COLORS[0]
        return {
          x: series.map((p) => p.step),
          y: series.map((p) => p.value),
          type: 'scatter' as const,
          mode: series.length <= 200 ? ('lines+markers' as const) : ('lines' as const),
          name: r.run_name,
          line: { color, width: 2 },
          marker: { color, size: 4 },
          hovertemplate: `<b>${r.run_name}</b><br>step=%{x}<br>value=%{y:.4f}<extra></extra>`,
        }
      })

    Plotly.react(
      ref.current,
      traces,
      {
        title: { text: metricName, font: { size: 14 } },
        margin: { t: 40, r: 16, b: 48, l: 60 },
        height: 280,
        paper_bgcolor: 'rgba(0,0,0,0)',
        plot_bgcolor: 'rgba(15,23,42,0.03)',
        showlegend: true,
        legend: { orientation: 'h' as const, y: -0.3 },
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
  }, [metricName, runs, colorMap])

  return <div ref={ref} style={{ width: '100%' }} />
}

// ── Main page ────────────────────────────────────────────────────────────────

export default function ComparisonPage() {
  const { experimentId } = useParams<{ experimentId: string }>()
  const navigate = useNavigate()

  const [selectedRunIds, setSelectedRunIds] = useState<string[]>([])
  const [selectedMetrics, setSelectedMetrics] = useState<string[]>([])
  const [comparisonResult, setComparisonResult] = useState<ComparisonResponse | null>(null)

  const { data: runsData, isLoading: runsLoading } = useQuery({
    queryKey: ['runs', experimentId],
    queryFn: () => runsApi.list(experimentId!, { page_size: 100 }),
    enabled: !!experimentId,
  })

  const runs = runsData?.runs ?? []

  // Available metric names — derived from comparison result once available,
  // or empty before first compare.
  const availableMetrics = useMemo<string[]>(() => {
    if (!comparisonResult) return []
    return comparisonResult.metric_names
  }, [comparisonResult])

  const compareMutation = useMutation({
    mutationFn: (body: { run_ids: string[]; metric_names: string[] }) =>
      comparisonApi.compare(experimentId!, body),
    onSuccess: (data) => {
      setComparisonResult(data)
      // auto-select all returned metric names on first compare
      if (selectedMetrics.length === 0) {
        setSelectedMetrics(data.metric_names)
      }
    },
  })

  function handleCompare() {
    if (selectedRunIds.length === 0) return
    compareMutation.mutate({
      run_ids: selectedRunIds,
      metric_names: selectedMetrics.length > 0 ? selectedMetrics : [],
    })
  }

  function handleMetricChange(event: SelectChangeEvent<string[]>) {
    const value = event.target.value
    setSelectedMetrics(typeof value === 'string' ? value.split(',') : value)
  }

  // Stable color map: run_id → color
  const colorMap = useMemo<Map<string, string>>(() => {
    const map = new Map<string, string>()
    runs.forEach((r, idx) => {
      map.set(r.id, PLOTLY_COLORS[idx % PLOTLY_COLORS.length])
    })
    return map
  }, [runs])

  const displayedMetrics = useMemo(() => {
    if (!comparisonResult) return []
    return selectedMetrics.length > 0
      ? selectedMetrics.filter((m) => comparisonResult.metric_names.includes(m))
      : comparisonResult.metric_names
  }, [comparisonResult, selectedMetrics])

  return (
    <Box sx={{ p: 3, maxWidth: 1200, mx: 'auto' }}>
      {/* Header */}
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 2, mb: 3 }}>
        <Button variant="text" onClick={() => navigate(`/experiments/${experimentId}`)}>
          ← Назад
        </Button>
        <Typography variant="h5" component="h1">
          Compare Runs
        </Typography>
      </Box>

      {/* Selectors */}
      <Paper sx={{ p: 2, mb: 3 }}>
        <Typography variant="subtitle1" sx={{ mb: 1.5, fontWeight: 600 }}>
          Выберите runs для сравнения
        </Typography>

        {runsLoading ? (
          <CircularProgress size={24} />
        ) : runs.length === 0 ? (
          <Typography color="text.secondary">Нет доступных runs</Typography>
        ) : (
          <Box sx={{ display: 'flex', flexWrap: 'wrap', gap: 1, mb: 2 }}>
            {runs.map((run) => (
              <FormControlLabel
                key={run.id}
                control={
                  <Checkbox
                    checked={selectedRunIds.includes(run.id)}
                    onChange={(e) => {
                      setSelectedRunIds((prev) =>
                        e.target.checked
                          ? [...prev, run.id]
                          : prev.filter((id) => id !== run.id)
                      )
                    }}
                    sx={{
                      color: colorMap.get(run.id),
                      '&.Mui-checked': { color: colorMap.get(run.id) },
                    }}
                  />
                }
                label={
                  <Box sx={{ display: 'flex', alignItems: 'center', gap: 0.5 }}>
                    <Typography variant="body2">{run.name}</Typography>
                    <Chip label={run.status} size="small" />
                  </Box>
                }
              />
            ))}
          </Box>
        )}

        {/* Metric selector — only shown after first compare */}
        {availableMetrics.length > 0 && (
          <FormControl sx={{ minWidth: 280, mb: 2 }} size="small">
            <InputLabel>Метрики</InputLabel>
            <Select<string[]>
              multiple
              value={selectedMetrics}
              onChange={handleMetricChange}
              input={<OutlinedInput label="Метрики" />}
              renderValue={(selected) => (
                <Box sx={{ display: 'flex', flexWrap: 'wrap', gap: 0.5 }}>
                  {(selected as string[]).map((v) => (
                    <Chip key={v} label={v} size="small" />
                  ))}
                </Box>
              )}
            >
              {availableMetrics.map((m) => (
                <MenuItem key={m} value={m}>
                  {m}
                </MenuItem>
              ))}
            </Select>
          </FormControl>
        )}

        <Box>
          <Button
            variant="contained"
            onClick={handleCompare}
            disabled={selectedRunIds.length === 0 || compareMutation.isPending}
            startIcon={compareMutation.isPending ? <CircularProgress size={16} color="inherit" /> : undefined}
          >
            Compare
          </Button>
          {selectedRunIds.length === 0 && (
            <Typography variant="caption" color="text.secondary" sx={{ ml: 2 }}>
              Выберите хотя бы один run
            </Typography>
          )}
        </Box>
      </Paper>

      {/* Error */}
      {compareMutation.isError && (
        <Alert severity="error" sx={{ mb: 2 }}>
          Не удалось загрузить сравнение
        </Alert>
      )}

      {/* Results */}
      {comparisonResult && (
        <>
          {/* Summary table */}
          <Paper sx={{ mb: 3 }}>
            <Box sx={{ p: 2, borderBottom: 1, borderColor: 'divider' }}>
              <Typography variant="subtitle1" fontWeight={600}>
                Summary
              </Typography>
            </Box>
            <TableContainer>
              <Table size="small">
                <TableHead>
                  <TableRow>
                    <TableCell>Run</TableCell>
                    <TableCell>Status</TableCell>
                    {displayedMetrics.map((m) => (
                      <TableCell key={m} colSpan={3} align="center">
                        {m}
                      </TableCell>
                    ))}
                  </TableRow>
                  <TableRow>
                    <TableCell />
                    <TableCell />
                    {displayedMetrics.map((m) => (
                      <>
                        <TableCell key={`${m}-last`} align="right" sx={{ color: 'text.secondary', fontSize: '0.75rem' }}>last</TableCell>
                        <TableCell key={`${m}-min`} align="right" sx={{ color: 'text.secondary', fontSize: '0.75rem' }}>min</TableCell>
                        <TableCell key={`${m}-max`} align="right" sx={{ color: 'text.secondary', fontSize: '0.75rem' }}>max</TableCell>
                      </>
                    ))}
                  </TableRow>
                </TableHead>
                <TableBody>
                  {comparisonResult.runs.map((entry) => (
                    <TableRow key={entry.run_id} hover>
                      <TableCell>
                        <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                          <Box
                            sx={{
                              width: 10,
                              height: 10,
                              borderRadius: '50%',
                              bgcolor: colorMap.get(entry.run_id) ?? '#636EFA',
                              flexShrink: 0,
                            }}
                          />
                          <Typography variant="body2">{entry.run_name}</Typography>
                        </Box>
                      </TableCell>
                      <TableCell>
                        <Chip label={entry.status} size="small" />
                      </TableCell>
                      {displayedMetrics.map((m) => {
                        const s = entry.metrics[m]?.summary
                        return (
                          <>
                            <TableCell key={`${entry.run_id}-${m}-last`} align="right">{fmt(s?.last)}</TableCell>
                            <TableCell key={`${entry.run_id}-${m}-min`} align="right">{fmt(s?.min)}</TableCell>
                            <TableCell key={`${entry.run_id}-${m}-max`} align="right">{fmt(s?.max)}</TableCell>
                          </>
                        )
                      })}
                    </TableRow>
                  ))}
                </TableBody>
              </Table>
            </TableContainer>
          </Paper>

          {/* Overlay charts */}
          <Typography variant="subtitle1" fontWeight={600} sx={{ mb: 1.5 }}>
            Overlay Charts
          </Typography>
          <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
            {displayedMetrics.map((m) => (
              <Paper key={m} sx={{ p: 2 }}>
                <OverlayChart
                  metricName={m}
                  runs={comparisonResult.runs}
                  colorMap={colorMap}
                />
              </Paper>
            ))}
          </Box>
        </>
      )}

      {/* Empty hint before first compare */}
      {!comparisonResult && !compareMutation.isPending && (
        <Box
          sx={{
            textAlign: 'center',
            py: 8,
            color: 'text.secondary',
          }}
        >
          <Typography>Выберите runs и нажмите «Compare» для сравнения метрик</Typography>
        </Box>
      )}
    </Box>
  )
}
