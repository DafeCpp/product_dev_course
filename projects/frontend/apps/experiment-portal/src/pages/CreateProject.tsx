import { useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { useMutation } from '@tanstack/react-query'
import { projectsApi } from '../api/client'
import type { ProjectCreate } from '../types'
import { Error, FormGroup, FormActions } from '../components/common'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'
import './CreateProject.scss'

function CreateProject() {
    const navigate = useNavigate()
    const [formData, setFormData] = useState<ProjectCreate>({
        name: '',
        description: '',
    })
    const [error, setError] = useState<string | null>(null)

    const createMutation = useMutation({
        mutationFn: (data: ProjectCreate) => projectsApi.create(data),
        onSuccess: () => {
            notifySuccess('Проект создан')
            // Переходим на список проектов после создания
            navigate('/projects')
        },
        onError: (err: any) => {
            const msg = err.response?.data?.error || 'Ошибка создания проекта'
            setError(msg)
            notifyError(msg)
        },
    })

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        setError(null)

        if (!formData.name.trim()) {
            const msg = 'Название проекта обязательно'
            setError(msg)
            notifyError(msg)
            return
        }

        createMutation.mutate({
            name: formData.name.trim(),
            description: formData.description || undefined,
        })
    }

    return (
        <div className="create-project">
            <h2>Создать проект</h2>

            {IS_TEST && error && <Error message={error} />}

            <form onSubmit={handleSubmit} className="project-form card">
                <FormGroup label="Название" required>
                    <input
                        type="text"
                        value={formData.name}
                        onChange={(e) => setFormData({ ...formData, name: e.target.value })}
                        required
                        placeholder="Например: Аэродинамические испытания"
                    />
                </FormGroup>

                <FormGroup label="Описание">
                    <textarea
                        value={formData.description}
                        onChange={(e) =>
                            setFormData({ ...formData, description: e.target.value })
                        }
                        placeholder="Описание проекта..."
                        rows={4}
                    />
                </FormGroup>

                <FormActions>
                    <button
                        type="button"
                        className="btn btn-secondary"
                        onClick={() => navigate('/projects')}
                    >
                        Отмена
                    </button>
                    <button
                        type="submit"
                        className="btn btn-primary"
                        disabled={createMutation.isPending}
                    >
                        {createMutation.isPending ? 'Создание...' : 'Создать проект'}
                    </button>
                </FormActions>
            </form>
        </div>
    )
}

export default CreateProject

