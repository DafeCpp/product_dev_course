import { useState } from 'react'
import { createPortal } from 'react-dom'
import { useQuery } from '@tanstack/react-query'
import { projectsApi } from '../api/client'
import { authApi } from '../api/auth'
import { Loading, Error, EmptyState, PageHeader } from '../components/common'
import CreateProjectModal from '../components/CreateProjectModal'
import ProjectMembersModal from '../components/ProjectMembersModal'
import './ProjectsList.css'

function ProjectsList() {
    const [isCreateModalOpen, setIsCreateModalOpen] = useState(false)
    const [selectedProjectId, setSelectedProjectId] = useState<string | null>(null)
    const [selectedProjectOwnerId, setSelectedProjectOwnerId] = useState<string | null>(null)
    const { data, isLoading, error, isError, status } = useQuery({
        queryKey: ['projects'],
        queryFn: async () => {
            console.log('ProjectsList: Starting fetch...')
            try {
                const result = await projectsApi.list()
                console.log('ProjectsList: Fetch success:', result)
                return result
            } catch (err) {
                console.error('ProjectsList: Fetch error:', err)
                throw err
            }
        },
        retry: 1,
        refetchOnWindowFocus: false,
        staleTime: 0, // Не кешировать данные
    })

    // Получаем текущего пользователя для проверки роли
    const { data: currentUser } = useQuery({
        queryKey: ['auth', 'me'],
        queryFn: () => authApi.me(),
    })

    // Отладочная информация
    console.log('ProjectsList render:', { isLoading, isError, status, hasData: !!data, data })

    // Показываем контент, если данные загружены
    const showContent = !isLoading && !isError && data

    const handleManageMembers = (projectId: string, ownerId: string) => {
        setSelectedProjectId(projectId)
        setSelectedProjectOwnerId(ownerId)
    }

    const handleCloseMembersModal = () => {
        setSelectedProjectId(null)
        setSelectedProjectOwnerId(null)
    }

    const isProjectOwner = (projectOwnerId: string) => {
        return currentUser?.id === projectOwnerId
    }

    return (
        <div className="projects-list">
            {isLoading && <Loading />}
            {isError && error && (
                <Error
                    message={
                        error instanceof Error
                            ? error.message
                            : 'Ошибка загрузки проектов'
                    }
                />
            )}

            {showContent && data && (
                <>
                    <PageHeader
                        title="Проекты"
                        action={
                            <button
                                className="btn btn-primary"
                                onClick={() => setIsCreateModalOpen(true)}
                            >
                                Создать проект
                            </button>
                        }
                    />

                    {data.projects.length === 0 ? (
                        <EmptyState message="У вас пока нет проектов">
                            <button
                                className="btn btn-primary"
                                onClick={() => setIsCreateModalOpen(true)}
                            >
                                Создать первый проект
                            </button>
                        </EmptyState>
                    ) : (
                        <div className="projects-grid">
                            {data.projects.map((project) => (
                                <div
                                    key={project.id}
                                    className="project-card card"
                                >
                                    <div className="project-card-header">
                                        <h3>{project.name}</h3>
                                        {isProjectOwner(project.owner_id) && (
                                            <button
                                                className="btn btn-sm btn-secondary"
                                                onClick={() => handleManageMembers(project.id, project.owner_id)}
                                                title="Управление участниками"
                                            >
                                                👥
                                            </button>
                                        )}
                                    </div>
                                    {project.description && (
                                        <p className="project-description">{project.description}</p>
                                    )}
                                    <div className="project-card-footer">
                                        <span className="project-meta">
                                            Создан: {new Date(project.created_at).toLocaleDateString('ru-RU')}
                                        </span>
                                    </div>
                                </div>
                            ))}
                        </div>
                    )}
                </>
            )}

            <CreateProjectModal
                isOpen={isCreateModalOpen}
                onClose={() => setIsCreateModalOpen(false)}
            />

            {selectedProjectId && selectedProjectOwnerId && (
                <ProjectMembersModal
                    isOpen={!!selectedProjectId}
                    onClose={handleCloseMembersModal}
                    projectId={selectedProjectId}
                    projectOwnerId={selectedProjectOwnerId}
                />
            )}

            {typeof document !== 'undefined' &&
                createPortal(
                    <button
                        className="fab"
                        onClick={() => setIsCreateModalOpen(true)}
                        title="Создать проект"
                        aria-label="Создать проект"
                    >
                        +
                    </button>,
                    document.body
                )}
        </div>
    )
}

export default ProjectsList

