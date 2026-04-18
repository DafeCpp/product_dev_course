import { useState } from 'react'
import Modal from './Modal'
import { telemetryExportApi } from '../api/client'
import type { CaptureSession, Sensor } from '../types'
import { notifyError, notifySuccess } from '../utils/notify'

export interface TelemetryExportModalProps {
    isOpen: boolean
    onClose: () => void
    runId: string
    /** 'session' → фиксированная сессия; 'run' → весь запуск (опциональный фильтр по сессии) */
    mode: 'session' | 'run'
    /** для mode='session': ID экспортируемой сессии */
    sessionId?: string
    /** для mode='session': порядковый номер (для имени файла) */
    sessionOrdinal?: number
    /** список сессий (для mode='run': dropdown фильтра; для mode='session': не нужен) */
    sessions?: CaptureSession[]
    sensors: Sensor[]
    /** предзаполненные значения (например, из TelemetryViewer) */
    initialCaptureSessionId?: string
    initialSensorId?: string
    initialRawOrPhysical?: 'raw' | 'physical' | 'both'
    initialIncludeLate?: boolean
    initialUseAggregation?: boolean
}

function TelemetryExportModal({
    isOpen,
    onClose,
    runId,
    mode,
    sessionId,
    sessionOrdinal,
    sessions = [],
    sensors,
    initialCaptureSessionId = '',
    initialSensorId = '',
    initialRawOrPhysical = 'both',
    initialIncludeLate = true,
    initialUseAggregation = false,
}: TelemetryExportModalProps) {
    const [format, setFormat] = useState<'csv' | 'json'>('csv')
    const [rawOrPhysical, setRawOrPhysical] = useState<'raw' | 'physical' | 'both'>(initialRawOrPhysical)
    const [includeLate, setIncludeLate] = useState(initialIncludeLate)
    const [useAggregation, setUseAggregation] = useState(initialUseAggregation)
    const [sensorId, setSensorId] = useState(initialSensorId)
    const [signal, setSignal] = useState('')
    const [filterSessionId, setFilterSessionId] = useState(
        mode === 'run' ? initialCaptureSessionId : (sessionId ?? '')
    )
    const [isExporting, setIsExporting] = useState(false)

    const handleClose = () => {
        if (!isExporting) onClose()
    }

    const getFilename = () => {
        const ext = format
        if (mode === 'session') {
            return `telemetry_session_${sessionOrdinal ?? sessionId}.${ext}`
        }
        return `telemetry_run_${runId}.${ext}`
    }

    const handleExport = async () => {
        setIsExporting(true)
        const params = {
            format,
            raw_or_physical: useAggregation ? undefined : rawOrPhysical,
            include_late: includeLate,
            aggregation: useAggregation ? ('1m' as const) : undefined,
            sensor_id: sensorId || undefined,
            signal: signal.trim() || undefined,
        }

        try {
            let data: string
            if (mode === 'session' && sessionId) {
                data = await telemetryExportApi.exportSession(runId, sessionId, params)
            } else {
                data = await telemetryExportApi.exportRun(runId, {
                    ...params,
                    capture_session_id: filterSessionId || undefined,
                })
            }

            const mime = format === 'json' ? 'application/json' : 'text/csv'
            const blob = new Blob([data], { type: mime })
            const url = URL.createObjectURL(blob)
            const a = document.createElement('a')
            a.href = url
            a.download = getFilename()
            document.body.appendChild(a)
            a.click()
            document.body.removeChild(a)
            URL.revokeObjectURL(url)

            notifySuccess('Телеметрия экспортирована')
            onClose()
        } catch (err: any) {
            notifyError(err?.response?.data?.message || err?.message || 'Ошибка экспорта телеметрии')
        } finally {
            setIsExporting(false)
        }
    }

    const title =
        mode === 'session'
            ? `Экспорт телеметрии — сессия #${sessionOrdinal ?? ''}`
            : 'Экспорт телеметрии — весь запуск'

    return (
        <Modal isOpen={isOpen} onClose={handleClose} title={title} disabled={isExporting} className="export-modal">
            <form className="modal-form" onSubmit={(e) => { e.preventDefault(); handleExport() }}>

                {/* Формат */}
                <div className="form-group">
                    <label>Формат файла</label>
                    <div className="export-modal__radio-row">
                        {(['csv', 'json'] as const).map((f) => (
                            <label key={f} className="export-modal__radio-label">
                                <input
                                    type="radio"
                                    name="export-format"
                                    value={f}
                                    checked={format === f}
                                    onChange={() => setFormat(f)}
                                    disabled={isExporting}
                                />
                                {f.toUpperCase()}
                            </label>
                        ))}
                    </div>
                </div>

                {/* Агрегация */}
                <div className="form-group">
                    <label className="export-modal__checkbox-label">
                        <input
                            type="checkbox"
                            checked={useAggregation}
                            onChange={(e) => setUseAggregation(e.target.checked)}
                            disabled={isExporting}
                        />
                        Агрегация 1m (avg / min / max по бакетам)
                    </label>
                    <span className="modal-form__hint">
                        При включении поле «Значения» игнорируется; в CSV — колонки avg/min/max.
                    </span>
                </div>

                {/* Значения (raw / physical / both) */}
                <div className="form-group">
                    <label>Значения</label>
                    <select
                        value={rawOrPhysical}
                        onChange={(e) => setRawOrPhysical(e.target.value as 'raw' | 'physical' | 'both')}
                        disabled={isExporting || useAggregation}
                    >
                        <option value="both">Raw + Physical</option>
                        <option value="raw">Только raw</option>
                        <option value="physical">Только physical</option>
                    </select>
                </div>

                {/* Включить late */}
                <div className="form-group">
                    <label className="export-modal__checkbox-label">
                        <input
                            type="checkbox"
                            checked={includeLate}
                            onChange={(e) => setIncludeLate(e.target.checked)}
                            disabled={isExporting}
                        />
                        Включить опоздавшие данные (late)
                    </label>
                </div>

                {/* Фильтр по датчику */}
                <div className="form-group">
                    <label>Датчик (фильтр)</label>
                    <select
                        value={sensorId}
                        onChange={(e) => setSensorId(e.target.value)}
                        disabled={isExporting}
                    >
                        <option value="">Все датчики</option>
                        {sensors.map((s) => (
                            <option key={s.id} value={s.id}>
                                {s.name} ({s.type})
                            </option>
                        ))}
                    </select>
                </div>

                {/* Фильтр по сигналу */}
                <div className="form-group">
                    <label>Сигнал (фильтр)</label>
                    <input
                        type="text"
                        value={signal}
                        onChange={(e) => setSignal(e.target.value)}
                        placeholder="Например: temperature"
                        disabled={isExporting}
                    />
                </div>

                {/* Фильтр по сессии (только для mode='run') */}
                {mode === 'run' && sessions.length > 0 && (
                    <div className="form-group">
                        <label>Сессия (фильтр)</label>
                        <select
                            value={filterSessionId}
                            onChange={(e) => setFilterSessionId(e.target.value)}
                            disabled={isExporting}
                        >
                            <option value="">Все сессии</option>
                            {sessions.map((s) => (
                                <option key={s.id} value={s.id}>
                                    #{s.ordinal_number} — {s.status}
                                    {s.started_at ? ` (${new Date(s.started_at).toLocaleString('ru')})` : ''}
                                </option>
                            ))}
                        </select>
                    </div>
                )}

                <div className="modal-actions">
                    <button
                        type="button"
                        className="btn btn-secondary"
                        onClick={handleClose}
                        disabled={isExporting}
                    >
                        Отмена
                    </button>
                    <button type="submit" className="btn btn-primary" disabled={isExporting}>
                        {isExporting ? 'Экспорт...' : `Скачать ${format.toUpperCase()}`}
                    </button>
                </div>
            </form>
        </Modal>
    )
}

export default TelemetryExportModal
