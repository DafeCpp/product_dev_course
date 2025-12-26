import { useQuery } from '@tanstack/react-query'
import { Link } from 'react-router-dom'
import { projectsApi } from '../api/client'
import { Loading, Error, EmptyState, PageHeader } from '../components/common'
import './ProjectsList.css'

function ProjectsList() {
    const { data, isLoading, error } = useQuery({
        queryKey: ['projects'],
        queryFn: () => projectsApi.list(),
    })

    if (isLoading) {
        return <Loading />
    }

    if (error) {
        return <Error message="Ошибка загрузки проектов" />
    }

    return (
        <div className="projects-list">
            <PageHeader
                title="Проекты"
                action={
                    <Link to="/projects/new" className="btn btn-primary">
                        Создать проект
                    </Link>
                }
            />

            {data && data.projects.length === 0 ? (
                <EmptyState
                    message="У вас пока нет проектов"
                    action={
                        <Link to="/projects/new" className="btn btn-primary">
                            Создать первый проект
                        </Link>
                    }
                />
            ) : (
                <div className="projects-grid">
                    {data?.projects.map((project) => (
                        <Link
                            key={project.id}
                            to={`/projects/${project.id}`}
                            className="project-card card"
                        >
                            <div className="project-card-header">
                                <h3>{project.name}</h3>
                            </div>
                            {project.description && (
                                <p className="project-description">{project.description}</p>
                            )}
                            <div className="project-card-footer">
                                <span className="project-meta">
                                    Создан: {new Date(project.created_at).toLocaleDateString('ru-RU')}
                                </span>
                            </div>
                        </Link>
                    ))}
                </div>
            )}
        </div>
    )
}

export default ProjectsList

