import { useState } from 'react'
import { useNavigate, Link } from 'react-router-dom'
import { useMutation, useQuery } from '@tanstack/react-query'
import { experimentsApi, projectsApi } from '../api/client'
import type { ExperimentCreate } from '../types'
import { setActiveProjectId } from '../utils/activeProject'
import { Error, FormGroup, FormActions, Loading, MaterialSelect } from '../components/common'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'
import './CreateExperiment.scss'

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
      notifySuccess('Эксперимент создан')
      navigate(`/experiments/${experiment.id}`)
    },
    onError: (err: any) => {
      const msg = err.response?.data?.error || 'Ошибка создания эксперимента'
      setError(msg)
      notifyError(msg)
    },
  })

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault()
    setError(null)

    if (!formData.project_id) {
      const msg = 'Выберите проект'
      setError(msg)
      notifyError(msg)
      return
    }

    if (!formData.name.trim()) {
      const msg = 'Название эксперимента обязательно'
      setError(msg)
      notifyError(msg)
      return
    }

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
      const msg = 'Неверный формат JSON в метаданных'
      setError(msg)
      notifyError(msg)
      return
    }

    createMutation.mutate({
      ...formData,
      name: formData.name.trim(),
      tags,
      metadata,
      description: formData.description || undefined,
      experiment_type: formData.experiment_type || undefined,
    })
  }

  return (
    <div className="create-experiment">
      <h2>Создать эксперимент</h2>

      {IS_TEST && error && <Error message={error} />}

      <form onSubmit={handleSubmit} className="experiment-form card">
        <FormGroup label="Проект" required>
          {projectsLoading ? (
            <Loading />
          ) : (
            <>
              <MaterialSelect
                id="create_experiment_project_id"
                value={formData.project_id}
                onChange={(id) => {
                  setFormData({ ...formData, project_id: id })
                  if (id) setActiveProjectId(id)
                }}
                required
              >
                <option value="">Выберите проект</option>
                {projectsData?.projects.map((project) => (
                  <option key={project.id} value={project.id}>
                    {project.name}
                  </option>
                ))}
              </MaterialSelect>
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
          <MaterialSelect
            id="create_experiment_type"
            value={formData.experiment_type}
            onChange={(value) =>
              setFormData({ ...formData, experiment_type: value })
            }
          >
            <option value="">Выберите тип</option>
            <option value="baseline">Бейзлайн (контроль)</option>
            <option value="benchmark">Бенчмарк / сравнение</option>
            <option value="validation">Валидация / проверка</option>
            <option value="calibration">Калибровка</option>
            <option value="demo">Демо</option>
            <option value="other">Другое</option>
          </MaterialSelect>
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

