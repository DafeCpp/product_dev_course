import { useState } from 'react'
import { useMutation } from '@tanstack/react-query'
import { telemetryApi } from '../api/client'
import type { TelemetryIngest } from '../types'
import Modal from './Modal'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'
import './TestTelemetryModal.scss'

interface TestTelemetryModalProps {
    sensorId: string
    sensorToken?: string | null
    isOpen: boolean
    onClose: () => void
}

function TestTelemetryModal({ sensorId, sensorToken, isOpen, onClose }: TestTelemetryModalProps) {
    const [token, setToken] = useState(sensorToken || '')
    const [metaJson, setMetaJson] = useState('{}')
    const [readingsJson, setReadingsJson] = useState('')
    const [error, setError] = useState<string | null>(null)
    const [success, setSuccess] = useState<string | null>(null)

    const ingestMutation = useMutation({
        mutationFn: (data: TelemetryIngest) => {
            if (!token) {
                throw new Error('Токен датчика обязателен')
            }
            return telemetryApi.ingest(data, token)
        },
        onSuccess: (response) => {
            setSuccess(`Телеметрия успешно отправлена. Принято записей: ${response.accepted}`)
            setError(null)
            notifySuccess('Телеметрия отправлена')
            // Очищаем форму через 2 секунды
            setTimeout(() => {
                setMetaJson('{}')
                setReadingsJson('')
                setSuccess(null)
            }, 2000)
        },
        onError: (err: any) => {
            const msg = err.response?.data?.error || err.message || 'Ошибка отправки телеметрии'
            setError(msg)
            setSuccess(null)
            notifyError(msg)
        },
    })

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        setError(null)
        setSuccess(null)

        if (!token.trim()) {
            const msg = 'Токен датчика обязателен'
            setError(msg)
            notifyError(msg)
            return
        }

        // Парсим JSON для meta
        let meta: Record<string, any> = {}
        if (metaJson.trim()) {
            try {
                meta = JSON.parse(metaJson)
            } catch (err) {
                const msg = 'Ошибка в формате JSON для meta'
                setError(msg)
                notifyError(msg)
                return
            }
        }

        // Парсим JSON для readings
        let readings: any[] = []
        if (readingsJson.trim()) {
            try {
                readings = JSON.parse(readingsJson)
                if (!Array.isArray(readings)) {
                    const msg = 'readings должен быть массивом'
                    setError(msg)
                    notifyError(msg)
                    return
                }
                // Валидация readings
                for (const reading of readings) {
                    if (!reading.timestamp || typeof reading.raw_value !== 'number') {
                        const msg = 'Каждый reading должен содержать timestamp и raw_value'
                        setError(msg)
                        notifyError(msg)
                        return
                    }
                }
            } catch (err) {
                const msg = 'Ошибка в формате JSON для readings'
                setError(msg)
                notifyError(msg)
                return
            }
        } else {
            // Если readings не указаны, создаем один тестовый reading
            readings = [
                {
                    timestamp: new Date().toISOString(),
                    raw_value: Math.random() * 100,
                    meta: {
                        signal: 'test.signal',
                    },
                },
            ]
        }

        const data: TelemetryIngest = {
            sensor_id: sensorId,
            meta: Object.keys(meta).length > 0 ? meta : undefined,
            readings,
        }

        ingestMutation.mutate(data)
    }

    const handleClose = () => {
        if (!ingestMutation.isPending) {
            setError(null)
            setSuccess(null)
            setMetaJson('{}')
            setReadingsJson('')
            onClose()
        }
    }

    const fillExample = () => {
        setReadingsJson(JSON.stringify(
            [
                {
                    timestamp: new Date().toISOString(),
                    raw_value: 25.5,
                    physical_value: 25.5,
                    meta: {
                        signal: 'temperature.c',
                    },
                },
                {
                    timestamp: new Date().toISOString(),
                    raw_value: 1013.25,
                    physical_value: 1013.25,
                    meta: {
                        signal: 'pressure.hpa',
                    },
                },
            ],
            null,
            2
        ))
        setMetaJson(JSON.stringify(
            {
                test: true,
                source: 'ui',
            },
            null,
            2
        ))
    }

    return (
        <Modal
            isOpen={isOpen}
            onClose={handleClose}
            title="Тестовая отправка телеметрии"
            disabled={ingestMutation.isPending}
            className="telemetry-modal"
        >
            <form onSubmit={handleSubmit} className="modal-form">
                {IS_TEST && error && <div className="error">{error}</div>}
                {success && <div className="success">{success}</div>}

                <div className="form-group">
                    <label htmlFor="telemetry_token">
                        Токен датчика <span className="required">*</span>
                    </label>
                    <input
                        id="telemetry_token"
                        type="text"
                        value={token}
                        onChange={(e) => setToken(e.target.value)}
                        required
                        placeholder="Введите токен датчика"
                        disabled={ingestMutation.isPending || !!sensorToken}
                    />
                    {sensorToken && (
                        <small className="form-hint">
                            Используется токен из контекста датчика
                        </small>
                    )}
                </div>

                <div className="form-group">
                    <label htmlFor="telemetry_meta">Meta (JSON, опционально)</label>
                    <textarea
                        id="telemetry_meta"
                        value={metaJson}
                        onChange={(e) => setMetaJson(e.target.value)}
                        placeholder='{"key": "value"}'
                        rows={4}
                        disabled={ingestMutation.isPending}
                    />
                    <small className="form-hint">
                        Метаданные для всего пакета телеметрии
                    </small>
                </div>

                <div className="form-group">
                    <label htmlFor="telemetry_readings">
                        Readings (JSON массив) <span className="required">*</span>
                    </label>
                    <textarea
                        id="telemetry_readings"
                        value={readingsJson}
                        onChange={(e) => setReadingsJson(e.target.value)}
                        placeholder='[{"timestamp": "2024-01-01T00:00:00Z", "raw_value": 25.5, "meta": {"signal": "test.signal"}}]'
                        rows={8}
                        disabled={ingestMutation.isPending}
                    />
                    <small className="form-hint">
                        Массив readings. Если пусто, будет создан один тестовый reading.
                        <button
                            type="button"
                            className="btn-link"
                            onClick={fillExample}
                            disabled={ingestMutation.isPending}
                        >
                            Заполнить примером
                        </button>
                    </small>
                </div>

                <div className="modal-actions">
                    <button
                        type="button"
                        className="btn btn-secondary"
                        onClick={handleClose}
                        disabled={ingestMutation.isPending}
                    >
                        Отмена
                    </button>
                    <button
                        type="submit"
                        className="btn btn-primary"
                        disabled={ingestMutation.isPending}
                    >
                        {ingestMutation.isPending ? 'Отправка...' : 'Отправить телеметрию'}
                    </button>
                </div>
            </form>
        </Modal>
    )
}

export default TestTelemetryModal

