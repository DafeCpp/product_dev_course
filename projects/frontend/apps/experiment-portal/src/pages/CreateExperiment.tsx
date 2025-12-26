import { useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { useMutation } from '@tanstack/react-query'
import { experimentsApi } from '../api/client'
import type { ExperimentCreate } from '../types'
import Error from '../components/Error'
import FormGroup from '../components/FormGroup'
import FormActions from '../components/FormActions'
import './CreateExperiment.css'

function CreateExperiment() {
  const navigate = useNavigate()
  const [formData, setFormData] = useState<ExperimentCreate>({
    project_id: '',
    name: '',
    description: '',
    experiment_type: '',
    tags: [],
    metadata: {},
  })
  const [tagsInput, setTagsInput] = useState('')
  const [metadataInput, setMetadataInput] = useState('{}')
  const [error, setError] = useState<string | null>(null)

  const createMutation = useMutation({
    mutationFn: (data: ExperimentCreate) => experimentsApi.create(data),
    onSuccess: (experiment) => {
      navigate(`/experiments/${experiment.id}`)
    },
    onError: (err: any) => {
      setError(err.response?.data?.error || 'Ошибка создания эксперимента')
    },
  })

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault()
    setError(null)

    // Парсинг тегов
    const tags = tagsInput
      .split(',')
      .map((t) => t.trim())
      .filter((t) => t.length > 0)

    // Парсинг metadata
    let metadata = {}
    try {
      metadata = JSON.parse(metadataInput)
    } catch (e) {
      setError('Неверный формат JSON в метаданных')
      return
    }

    createMutation.mutate({
      ...formData,
      tags,
      metadata,
      description: formData.description || undefined,
      experiment_type: formData.experiment_type || undefined,
    })
  }

  return (
    <div className="create-experiment">
      <h2>Создать эксперимент</h2>

      {error && <Error message={error} />}

      <form onSubmit={handleSubmit} className="experiment-form card">
        <FormGroup label="Project ID" required>
          <input
            type="text"
            value={formData.project_id}
            onChange={(e) =>
              setFormData({ ...formData, project_id: e.target.value })
            }
            required
            placeholder="UUID проекта"
          />
        </FormGroup>

        <FormGroup label="Название" required>
          <input
            type="text"
            value={formData.name}
            onChange={(e) => setFormData({ ...formData, name: e.target.value })}
            required
            placeholder="Например: Аэродинамические испытания крыла"
          />
        </FormGroup>

        <FormGroup label="Описание">
          <textarea
            value={formData.description}
            onChange={(e) =>
              setFormData({ ...formData, description: e.target.value })
            }
            placeholder="Детальное описание эксперимента..."
          />
        </FormGroup>

        <FormGroup label="Тип эксперимента">
          <select
            value={formData.experiment_type}
            onChange={(e) =>
              setFormData({ ...formData, experiment_type: e.target.value })
            }
          >
            <option value="">Выберите тип</option>
            <option value="aerodynamics">Аэродинамика</option>
            <option value="strength">Прочность</option>
            <option value="thermal">Термические</option>
            <option value="vibration">Вибрационные</option>
            <option value="other">Другое</option>
          </select>
        </FormGroup>

        <FormGroup
          label="Теги"
          hint="Введите теги через запятую для удобной фильтрации"
        >
          <input
            type="text"
            value={tagsInput}
            onChange={(e) => setTagsInput(e.target.value)}
            placeholder="Через запятую: аэродинамика, крыло, naca"
          />
        </FormGroup>

        <FormGroup
          label="Метаданные (JSON)"
          hint="Дополнительные данные эксперимента в формате JSON"
        >
          <textarea
            value={metadataInput}
            onChange={(e) => setMetadataInput(e.target.value)}
            placeholder='{"wind_speed": "30 m/s", "angle_of_attack": "0-15 deg"}'
            style={{ fontFamily: 'monospace', fontSize: '12px' }}
          />
        </FormGroup>

        <FormActions
          onCancel={() => navigate('/experiments')}
          submitLabel="Создать эксперимент"
          isSubmitting={createMutation.isPending}
        />
      </form>
    </div>
  )
}

export default CreateExperiment

