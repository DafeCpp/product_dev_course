import { useEffect, useState } from 'react'

export type TelemetryViewMode = 'live' | 'history'

interface StoredTelemetryViewerState {
    projectId: string
    experimentId: string
    runId: string
    viewMode: TelemetryViewMode
}

export default function useTelemetryViewerState() {
    const [projectId, setProjectId] = useState('')
    const [experimentId, setExperimentId] = useState('')
    const [runId, setRunId] = useState('')
    const [viewMode, setViewMode] = useState<TelemetryViewMode>('live')
    const [loaded, setLoaded] = useState(false)

    useEffect(() => {
        try {
            const state = JSON.parse(window.localStorage.getItem('telemetry_viewer_state') || '{}') as Partial<StoredTelemetryViewerState>
            if (typeof state.projectId === 'string') setProjectId(state.projectId)
            if (typeof state.experimentId === 'string') setExperimentId(state.experimentId)
            if (typeof state.runId === 'string') setRunId(state.runId)
            if (state.viewMode === 'live' || state.viewMode === 'history') setViewMode(state.viewMode)
        } catch {
            // Ignore malformed persisted state.
        } finally {
            setLoaded(true)
        }
    }, [])

    useEffect(() => {
        if (!loaded) return
        window.localStorage.setItem('telemetry_viewer_state', JSON.stringify({ projectId, experimentId, runId, viewMode }))
    }, [experimentId, loaded, projectId, runId, viewMode])

    return { projectId, setProjectId, experimentId, setExperimentId, runId, setRunId, viewMode, setViewMode }
}
