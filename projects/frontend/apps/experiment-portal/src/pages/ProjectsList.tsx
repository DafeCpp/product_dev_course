import { useState } from 'react'
import { createPortal } from 'react-dom'
import { useQuery } from '@tanstack/react-query'
import { projectsApi } from '../api/client'
import { authApi } from '../api/auth'
import { Loading, Error, EmptyState, FloatingActionButton } from '../components/common'
import ProjectModal from '../components/ProjectModal'
import ProjectMembersModal from '../components/ProjectMembersModal'
import './ProjectsList.css'

function ProjectsList() {
    const [selectedProjectId, setSelectedProjectId] = useState<string | null>(null)
    const [selectedProjectOwnerId, setSelectedProjectOwnerId] = useState<string | null>(null)
    const [projectModal, setProjectModal] = useState<{
        isOpen: boolean
        mode: 'create' | 'view' | 'edit'
        projectId?: string
    }>({ isOpen: false, mode: 'create' })
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
    const { data: currentUser, isLoading: userLoading } = useQuery({
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

    const openCreateProject = () => setProjectModal({ isOpen: true, mode: 'create' })
    const openProject = (projectId: string, ownerId: string) => {
        // Пока в списке у нас есть только owner_id; если владелец — даём edit, иначе view.
        const mode = currentUser?.id === ownerId ? 'edit' : 'view'
        setProjectModal({ isOpen: true, mode, projectId })
    }
    const closeProjectModal = () => setProjectModal((prev) => ({ ...prev, isOpen: false }))

    const isProjectOwner = (projectOwnerId: string) => {
        return currentUser?.id === projectOwnerId
    }
    const actionsDisabled = isLoading || userLoading

    return (
        <div className="projects-list">
            {isLoading && <Loading message="Загрузка проектов..." />}
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
                    {data.projects.length === 0 ? (
                        <EmptyState message="У вас пока нет проектов">
                            <button
                                className="btn btn-primary"
                                onClick={openCreateProject}
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
                                        <div className="project-card-actions">
                                            <button
                                                className="btn btn-sm btn-secondary"
                                                onClick={() => openProject(project.id, project.owner_id)}
                                                title={isProjectOwner(project.owner_id) ? 'Просмотр и редактирование' : 'Просмотр'}
                                                aria-label="Открыть проект"
                                                disabled={actionsDisabled}
                                            >
                                                {isProjectOwner(project.owner_id) ? '✏️' : 'ℹ️'}
                                            </button>
                                            {isProjectOwner(project.owner_id) && (
                                                <button
                                                    className="btn btn-sm btn-secondary"
                                                    onClick={() => handleManageMembers(project.id, project.owner_id)}
                                                    title="Управление участниками"
                                                    aria-label="Управление участниками"
                                                    disabled={actionsDisabled}
                                                >
                                                    👥
                                                </button>
                                            )}
                                        </div>
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

            <ProjectModal
                isOpen={projectModal.isOpen}
                onClose={closeProjectModal}
                mode={projectModal.mode}
                projectId={projectModal.projectId}
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
                    <FloatingActionButton
                        onClick={openCreateProject}
                        title="Создать проект"
                        ariaLabel="Создать проект"
                    />,
                    document.body
                )}
        </div>
    )
}

export default ProjectsList

