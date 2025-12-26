import { useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { useMutation } from '@tanstack/react-query'
import { sensorsApi } from '../api/client'
import type { SensorCreate, SensorRegisterResponse } from '../types'
import './CreateSensor.css'

function CreateSensor() {
    const navigate = useNavigate()
    const [formData, setFormData] = useState<SensorCreate>({
        project_id: '',
        name: '',
        type: '',
        input_unit: '',
        display_unit: '',
        calibration_notes: '',
    })
    const [showToken, setShowToken] = useState(false)
    const [error, setError] = useState<string | null>(null)

    const createMutation = useMutation({
        mutationFn: (data: SensorCreate) => sensorsApi.create(data),
        onSuccess: (response: SensorRegisterResponse) => {
            // Показываем токен пользователю
            setShowToken(true)
            // Можно сохранить токен в state для отображения
            setTimeout(() => {
                navigate(`/sensors/${response.sensor.id}`)
            }, 3000) // Через 3 секунды переходим на страницу датчика
        },
        onError: (err: any) => {
            setError(err.response?.data?.error || 'Ошибка регистрации датчика')
        },
    })

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        setError(null)

        createMutation.mutate({
            ...formData,
            calibration_notes: formData.calibration_notes || undefined,
        })
    }

    const handleCopyToken = () => {
        if (createMutation.data?.token) {
            navigator.clipboard.writeText(createMutation.data.token)
            alert('Токен скопирован в буфер обмена')
        }
    }

    if (showToken && createMutation.data) {
        return (
            <div className="create-sensor">
                <div className="token-display card">
                    <h2>Датчик успешно зарегистрирован!</h2>
                    <p className="warning">
                        ⚠️ Сохраните токен сейчас! Он больше не будет показан.
                    </p>
                    <div className="token-box">
                        <label>Токен датчика:</label>
                        <div className="token-value">
                            <code>{createMutation.data.token}</code>
                            <button
                                type="button"
                                className="btn btn-secondary btn-sm"
                                onClick={handleCopyToken}
                            >
                                Копировать
                            </button>
                        </div>
                    </div>
                    <p className="redirect-info">
                        Через несколько секунд вы будете перенаправлены на страницу датчика...
                    </p>
                    <button
                        type="button"
                        className="btn btn-primary"
                        onClick={() => navigate(`/sensors/${createMutation.data.sensor.id}`)}
                    >
                        Перейти к датчику
                    </button>
                </div>
            </div>
        )
    }

    return (
        <div className="create-sensor">
            <h2>Зарегистрировать датчик</h2>

            {error && <div className="error">{error}</div>}

            <form onSubmit={handleSubmit} className="sensor-form card">
                <div className="form-group">
                    <label htmlFor="project_id">
                        Project ID <span className="required">*</span>
                    </label>
                    <input
                        id="project_id"
                        type="text"
                        value={formData.project_id}
                        onChange={(e) =>
                            setFormData({ ...formData, project_id: e.target.value })
                        }
                        required
                        placeholder="UUID проекта"
                    />
                </div>

                <div className="form-group">
                    <label htmlFor="sensor_name">
                        Название <span className="required">*</span>
                    </label>
                    <input
                        id="sensor_name"
                        type="text"
                        value={formData.name}
                        onChange={(e) => setFormData({ ...formData, name: e.target.value })}
                        required
                        placeholder="Например: Датчик температуры #1"
                    />
                </div>

                <div className="form-group">
                    <label htmlFor="sensor_type">
                        Тип датчика <span className="required">*</span>
                    </label>
                    <select
                        id="sensor_type"
                        value={formData.type}
                        onChange={(e) => setFormData({ ...formData, type: e.target.value })}
                        required
                    >
                        <option value="">Выберите тип</option>
                        <option value="temperature">Температура</option>
                        <option value="pressure">Давление</option>
                        <option value="vibration">Вибрация</option>
                        <option value="current">Ток</option>
                        <option value="voltage">Напряжение</option>
                        <option value="humidity">Влажность</option>
                        <option value="other">Другое</option>
                    </select>
                </div>

                <div className="form-group">
                    <label htmlFor="input_unit">
                        Входная единица измерения <span className="required">*</span>
                    </label>
                    <input
                        id="input_unit"
                        type="text"
                        value={formData.input_unit}
                        onChange={(e) =>
                            setFormData({ ...formData, input_unit: e.target.value })
                        }
                        required
                        placeholder="Например: V (вольты), mV, ADC"
                    />
                    <small className="form-hint">
                        Единица измерения сырых данных от датчика
                    </small>
                </div>

                <div className="form-group">
                    <label htmlFor="display_unit">
                        Единица отображения <span className="required">*</span>
                    </label>
                    <input
                        id="display_unit"
                        type="text"
                        value={formData.display_unit}
                        onChange={(e) =>
                            setFormData({ ...formData, display_unit: e.target.value })
                        }
                        required
                        placeholder="Например: °C, Pa, Hz"
                    />
                    <small className="form-hint">
                        Единица измерения для отображения в интерфейсе
                    </small>
                </div>

                <div className="form-group">
                    <label htmlFor="calibration_notes">Заметки по калибровке</label>
                    <textarea
                        id="calibration_notes"
                        value={formData.calibration_notes}
                        onChange={(e) =>
                            setFormData({ ...formData, calibration_notes: e.target.value })
                        }
                        placeholder="Дополнительная информация о калибровке датчика..."
                        rows={3}
                    />
                </div>

                <div className="form-actions">
                    <button
                        type="button"
                        className="btn btn-secondary"
                        onClick={() => navigate('/sensors')}
                    >
                        Отмена
                    </button>
                    <button
                        type="submit"
                        className="btn btn-primary"
                        disabled={createMutation.isPending}
                    >
                        {createMutation.isPending ? 'Регистрация...' : 'Зарегистрировать датчик'}
                    </button>
                </div>
            </form>
        </div>
    )
}

export default CreateSensor

