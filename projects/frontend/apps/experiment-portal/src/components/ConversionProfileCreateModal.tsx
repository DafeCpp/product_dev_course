import { useState, useMemo } from 'react'
import { useMutation, useQueryClient } from '@tanstack/react-query'
import { conversionProfilesApi } from '../api/client'
import type { ConversionProfileInput } from '../types'
import Modal from './Modal'
import { notifyError, notifySuccess } from '../utils/notify'

interface ConversionProfileCreateModalProps {
    sensorId: string
    isOpen: boolean
    onClose: () => void
}

type ProfileKind = 'linear' | 'polynomial' | 'lookup_table'

/** Client-side conversion preview (mirrors backend logic). */
function previewConversion(kind: ProfileKind, payload: Record<string, any>, rawValue: number): number | null {
    if (kind === 'linear') {
        const a = Number(payload.a)
        const b = Number(payload.b)
        if (isNaN(a) || isNaN(b)) return null
        return a * rawValue + b
    }
    if (kind === 'polynomial') {
        const coefficients = payload.coefficients as number[] | undefined
        if (!Array.isArray(coefficients) || coefficients.length === 0) return null
        let result = 0
        let power = 1
        for (const c of coefficients) {
            if (isNaN(Number(c))) return null
            result += Number(c) * power
            power *= rawValue
        }
        return result
    }
    if (kind === 'lookup_table') {
        const table = payload.table as Array<{ raw: number; physical: number }> | undefined
        if (!Array.isArray(table) || table.length < 2) return null
        const sorted = [...table].sort((a, b) => Number(a.raw) - Number(b.raw))
        if (rawValue <= Number(sorted[0].raw)) return Number(sorted[0].physical)
        if (rawValue >= Number(sorted[sorted.length - 1].raw)) return Number(sorted[sorted.length - 1].physical)
        for (let i = 0; i < sorted.length - 1; i++) {
            const x0 = Number(sorted[i].raw), y0 = Number(sorted[i].physical)
            const x1 = Number(sorted[i + 1].raw), y1 = Number(sorted[i + 1].physical)
            if (x0 <= rawValue && rawValue <= x1) {
                const t = x1 !== x0 ? (rawValue - x0) / (x1 - x0) : 0
                return y0 + t * (y1 - y0)
            }
        }
        return null
    }
    return null
}

function ConversionProfileCreateModal({ sensorId, isOpen, onClose }: ConversionProfileCreateModalProps) {
    const queryClient = useQueryClient()
    const [version, setVersion] = useState('')
    const [kind, setKind] = useState<ProfileKind>('linear')
    const [error, setError] = useState<string | null>(null)

    // Linear payload
    const [linearA, setLinearA] = useState('1')
    const [linearB, setLinearB] = useState('0')

    // Polynomial payload
    const [coefficients, setCoefficients] = useState<string[]>(['0', '1'])

    // Lookup table payload
    const [tablePoints, setTablePoints] = useState<Array<{ raw: string; physical: string }>>([
        { raw: '0', physical: '0' },
        { raw: '100', physical: '100' },
    ])

    // Preview
    const [previewRaw, setPreviewRaw] = useState('10')

    const buildPayload = (): Record<string, any> | null => {
        if (kind === 'linear') {
            const a = Number(linearA)
            const b = Number(linearB)
            if (isNaN(a) || isNaN(b)) return null
            return { a, b }
        }
        if (kind === 'polynomial') {
            const nums = coefficients.map(Number)
            if (nums.some(isNaN) || nums.length === 0) return null
            return { coefficients: nums }
        }
        if (kind === 'lookup_table') {
            const table = tablePoints.map(p => ({
                raw: Number(p.raw),
                physical: Number(p.physical),
            }))
            if (table.some(p => isNaN(p.raw) || isNaN(p.physical))) return null
            if (table.length < 2) return null
            return { table }
        }
        return null
    }

    const previewResult = useMemo(() => {
        const payload = buildPayload()
        const raw = Number(previewRaw)
        if (payload === null || isNaN(raw)) return null
        return previewConversion(kind, payload, raw)
    }, [kind, linearA, linearB, coefficients, tablePoints, previewRaw])

    const createMutation = useMutation({
        mutationFn: (data: ConversionProfileInput) => conversionProfilesApi.create(sensorId, data),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['sensor', sensorId, 'profiles'] })
            notifySuccess('Профиль преобразования создан')
            handleClose()
        },
        onError: (err: any) => {
            const msg = err.response?.data?.error || 'Ошибка создания профиля'
            setError(msg)
            notifyError(msg)
        },
    })

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        setError(null)

        if (!version.trim()) {
            const msg = 'Версия обязательна'
            setError(msg)
            notifyError(msg)
            return
        }

        const payload = buildPayload()
        if (payload === null) {
            const msg = 'Некорректные параметры преобразования'
            setError(msg)
            notifyError(msg)
            return
        }

        createMutation.mutate({
            version: version.trim(),
            kind,
            payload,
        })
    }

    const handleClose = () => {
        if (!createMutation.isPending) {
            setVersion('')
            setKind('linear')
            setLinearA('1')
            setLinearB('0')
            setCoefficients(['0', '1'])
            setTablePoints([
                { raw: '0', physical: '0' },
                { raw: '100', physical: '100' },
            ])
            setPreviewRaw('10')
            setError(null)
            onClose()
        }
    }

    return (
        <Modal
            isOpen={isOpen}
            onClose={handleClose}
            title="Создать профиль преобразования"
            disabled={createMutation.isPending}
        >
            <form onSubmit={handleSubmit} className="modal-form">
                {error && <div className="error">{error}</div>}

                <div className="form-group">
                    <label htmlFor="profile_version">
                        Версия <span className="required">*</span>
                    </label>
                    <input
                        id="profile_version"
                        type="text"
                        value={version}
                        onChange={(e) => setVersion(e.target.value)}
                        required
                        placeholder="Например: v1.0"
                        disabled={createMutation.isPending}
                    />
                </div>

                <div className="form-group">
                    <label htmlFor="profile_kind">Тип преобразования</label>
                    <select
                        id="profile_kind"
                        value={kind}
                        onChange={(e) => setKind(e.target.value as ProfileKind)}
                        disabled={createMutation.isPending}
                    >
                        <option value="linear">Линейное (a·x + b)</option>
                        <option value="polynomial">Полиномиальное</option>
                        <option value="lookup_table">Таблица (интерполяция)</option>
                    </select>
                </div>

                {/* Linear payload */}
                {kind === 'linear' && (
                    <div className="form-group">
                        <label>Параметры: physical = a · raw + b</label>
                        <div style={{ display: 'flex', gap: '8px' }}>
                            <div style={{ flex: 1 }}>
                                <label htmlFor="linear_a">a</label>
                                <input
                                    id="linear_a"
                                    type="number"
                                    step="any"
                                    value={linearA}
                                    onChange={(e) => setLinearA(e.target.value)}
                                    disabled={createMutation.isPending}
                                />
                            </div>
                            <div style={{ flex: 1 }}>
                                <label htmlFor="linear_b">b</label>
                                <input
                                    id="linear_b"
                                    type="number"
                                    step="any"
                                    value={linearB}
                                    onChange={(e) => setLinearB(e.target.value)}
                                    disabled={createMutation.isPending}
                                />
                            </div>
                        </div>
                    </div>
                )}

                {/* Polynomial payload */}
                {kind === 'polynomial' && (
                    <div className="form-group">
                        <label>Коэффициенты: c₀ + c₁·x + c₂·x² + ...</label>
                        {coefficients.map((c, i) => (
                            <div key={i} style={{ display: 'flex', gap: '4px', marginBottom: '4px', alignItems: 'center' }}>
                                <span style={{ minWidth: '24px' }}>c{i}</span>
                                <input
                                    type="number"
                                    step="any"
                                    value={c}
                                    onChange={(e) => {
                                        const newCoeffs = [...coefficients]
                                        newCoeffs[i] = e.target.value
                                        setCoefficients(newCoeffs)
                                    }}
                                    disabled={createMutation.isPending}
                                    style={{ flex: 1 }}
                                />
                                {coefficients.length > 1 && (
                                    <button
                                        type="button"
                                        className="btn btn-secondary btn-sm"
                                        onClick={() => setCoefficients(coefficients.filter((_, j) => j !== i))}
                                        disabled={createMutation.isPending}
                                    >
                                        &times;
                                    </button>
                                )}
                            </div>
                        ))}
                        <button
                            type="button"
                            className="btn btn-secondary btn-sm"
                            onClick={() => setCoefficients([...coefficients, '0'])}
                            disabled={createMutation.isPending}
                        >
                            + Добавить коэффициент
                        </button>
                    </div>
                )}

                {/* Lookup table payload */}
                {kind === 'lookup_table' && (
                    <div className="form-group">
                        <label>Таблица преобразования (линейная интерполяция)</label>
                        <div style={{ display: 'flex', gap: '8px', marginBottom: '4px' }}>
                            <span style={{ flex: 1, fontWeight: 'bold', fontSize: '0.85em' }}>Raw</span>
                            <span style={{ flex: 1, fontWeight: 'bold', fontSize: '0.85em' }}>Physical</span>
                            <span style={{ width: '30px' }}></span>
                        </div>
                        {tablePoints.map((point, i) => (
                            <div key={i} style={{ display: 'flex', gap: '8px', marginBottom: '4px', alignItems: 'center' }}>
                                <input
                                    type="number"
                                    step="any"
                                    value={point.raw}
                                    onChange={(e) => {
                                        const newPoints = [...tablePoints]
                                        newPoints[i] = { ...newPoints[i], raw: e.target.value }
                                        setTablePoints(newPoints)
                                    }}
                                    disabled={createMutation.isPending}
                                    style={{ flex: 1 }}
                                />
                                <input
                                    type="number"
                                    step="any"
                                    value={point.physical}
                                    onChange={(e) => {
                                        const newPoints = [...tablePoints]
                                        newPoints[i] = { ...newPoints[i], physical: e.target.value }
                                        setTablePoints(newPoints)
                                    }}
                                    disabled={createMutation.isPending}
                                    style={{ flex: 1 }}
                                />
                                {tablePoints.length > 2 && (
                                    <button
                                        type="button"
                                        className="btn btn-secondary btn-sm"
                                        onClick={() => setTablePoints(tablePoints.filter((_, j) => j !== i))}
                                        disabled={createMutation.isPending}
                                        style={{ width: '30px' }}
                                    >
                                        &times;
                                    </button>
                                )}
                            </div>
                        ))}
                        <button
                            type="button"
                            className="btn btn-secondary btn-sm"
                            onClick={() => setTablePoints([...tablePoints, { raw: '', physical: '' }])}
                            disabled={createMutation.isPending}
                        >
                            + Добавить точку
                        </button>
                    </div>
                )}

                {/* Live preview */}
                <div className="form-group" style={{ background: 'var(--bg-secondary, #f5f5f5)', padding: '12px', borderRadius: '6px' }}>
                    <label>Предпросмотр</label>
                    <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                        <span>raw =</span>
                        <input
                            type="number"
                            step="any"
                            value={previewRaw}
                            onChange={(e) => setPreviewRaw(e.target.value)}
                            style={{ width: '100px' }}
                        />
                        <span>&rarr; physical =</span>
                        <strong>{previewResult !== null ? previewResult.toFixed(4) : '—'}</strong>
                    </div>
                </div>

                <div className="modal-actions">
                    <button
                        type="button"
                        className="btn btn-secondary"
                        onClick={handleClose}
                        disabled={createMutation.isPending}
                    >
                        Отмена
                    </button>
                    <button
                        type="submit"
                        className="btn btn-primary"
                        disabled={createMutation.isPending}
                    >
                        {createMutation.isPending ? 'Создание...' : 'Создать профиль'}
                    </button>
                </div>
            </form>
        </Modal>
    )
}

export default ConversionProfileCreateModal
