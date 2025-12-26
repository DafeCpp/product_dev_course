import { useState } from 'react'
import { useNavigate, Link } from 'react-router-dom'
import { useMutation, useQuery } from '@tanstack/react-query'
import { experimentsApi, projectsApi } from '../api/client'
import type { ExperimentCreate } from '../types'
import { Error, FormGroup, FormActions, Loading } from '../components/common'
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

  const { data: projectsData, isLoading: projectsLoading } = useQuery({
    queryKey: ['projects'],
    queryFn: () => projectsApi.list(),
  })

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
        <FormGroup label="Проект" required>
          {projectsLoading ? (
            <Loading />
          ) : (
            <>
              <select
                value={formData.project_id}
                onChange={(e) =>
                  setFormData({ ...formData, project_id: e.target.value })
                }
                required
              >
                <option value="">Выберите проект</option>
                {projectsData?.projects.map((project) => (
                  <option key={project.id} value={project.id}>
                    {project.name}
                  </option>
                ))}
              </select>
              {projectsData?.projects.length === 0 && (
                <div style={{ marginTop: '0.5rem', fontSize: '0.875rem', color: '#666' }}>
                  У вас нет проектов.{' '}
                  <Link to="/projects/new" style={{ color: 'var(--primary-color, #007bff)' }}>
                    Создать проект
                  </Link>
                </div>
              )}
            </>
          )}
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

        <FormActions>
          <button
            type="button"
            className="btn btn-secondary"
            onClick={() => navigate('/experiments')}
          >
            Отмена
          </button>
          <button
            type="submit"
            className="btn btn-primary"
            disabled={createMutation.isPending}
          >
            {createMutation.isPending ? 'Создание...' : 'Создать эксперимент'}
          </button>
        </FormActions>
      </form>
    </div>
  )
}

export default CreateExperiment

