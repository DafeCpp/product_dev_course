import { EmptyState } from '../common'
import TelemetryPanel from '../TelemetryPanel'
import type { RefObject } from 'react'
import type { Sensor } from '../../types'

interface TelemetryPanelGridProps {
    panelIds: string[]; sensors: Sensor[]; sensorsLoading: boolean; sensorsError: unknown; titleSeed: string
    panelSizes: Record<string, { width: number; height: number }>; containerWidth: number; containerRef: RefObject<HTMLDivElement | null>
    draggingId: string | null; dragOverId: string | null
    onRemove: (id: string) => void; onMove: (source: string, target: string) => void; onSizeChange: (id: string, size: { width: number; height: number }) => void; onRecordReceived: (sensorId: string, value: number) => void
    onDraggingChange: (id: string | null) => void; onDragOverChange: (id: string | null) => void
}

export default function TelemetryPanelGrid({ panelIds, sensors, sensorsLoading, sensorsError, titleSeed, panelSizes, containerWidth, containerRef, draggingId, dragOverId, onRemove, onMove, onSizeChange, onRecordReceived, onDraggingChange, onDragOverChange }: TelemetryPanelGridProps) {
    if (panelIds.length === 0) return <EmptyState message="Добавьте панель, чтобы начать просмотр графиков." />
    const error = sensorsError ? (typeof sensorsError === 'string' ? sensorsError : (sensorsError as Error).message || 'Ошибка загрузки сенсоров') : null
    return <div className="telemetry-view__panels" ref={containerRef}>{panelIds.map((panelId, index) => {
        const size = panelSizes[panelId]; const isWide = Boolean(size && containerWidth > 0 && size.width > containerWidth / 2)
        return <div key={panelId} className={`telemetry-view__panel-item${draggingId === panelId ? ' telemetry-view__panel-item--dragging' : ''}${dragOverId === panelId ? ' telemetry-view__panel-item--over' : ''}${isWide ? ' telemetry-view__panel-item--full' : ''}`} onDragOver={(event) => { if (!draggingId || draggingId === panelId) return; event.preventDefault(); event.dataTransfer.dropEffect = 'move'; onDragOverChange(panelId) }} onDragLeave={(event) => { if (!event.currentTarget.contains(event.relatedTarget as Node)) onDragOverChange(dragOverId === panelId ? null : dragOverId) }} onDrop={(event) => { event.preventDefault(); if (draggingId) onMove(draggingId, panelId); onDraggingChange(null); onDragOverChange(null) }}>
            <TelemetryPanel panelId={panelId} sensors={sensors} sensorsLoading={sensorsLoading} sensorsError={error} title={`${titleSeed} #${index + 1}`} onRemove={() => onRemove(panelId)} onSizeChange={(next) => onSizeChange(panelId, next)} onRecordReceived={onRecordReceived} dragHandleProps={{ draggable: true, onDragStart: (event) => { onDraggingChange(panelId); event.dataTransfer.effectAllowed = 'move'; event.dataTransfer.setData('text/plain', panelId) }, onDragEnd: () => { onDraggingChange(null); onDragOverChange(null) } }} />
        </div>
    })}</div>
}
