import { useQuery, useQueries } from '@tanstack/react-query'
import { format } from 'date-fns'
import { projectsApi } from '../api/client'
import { authApi } from '../api/auth'
import Modal from './Modal'
import { Loading, Error, EmptyState, InfoRow } from './common'
import './CreateRunModal.css'

interface UserProfileModalProps {
    isOpen: boolean
    onClose: () => void
}

function UserProfileModal({ isOpen, onClose }: UserProfileModalProps) {
    // Получаем информацию о текущем пользователе
    const {
        data: user,
        isLoading: userLoading,
        isError: userError,
        error: userErrorData,
    } = useQuery({
        queryKey: ['auth', 'me'],
        queryFn: () => authApi.me(),
        enabled: isOpen,
    })

    // Получаем список проектов пользователя
    const {
        data: projectsData,
        isLoading: projectsLoading,
        isError: projectsError,
        error: projectsErrorData,
    } = useQuery({
        queryKey: ['projects'],
        queryFn: () => projectsApi.list(),
        enabled: isOpen,
    })

    // Получаем роли пользователя в каждом проекте
    const memberQueries = useQueries({
        queries:
            projectsData?.projects.map((project) => ({
                queryKey: ['projects', project.id, 'members'],
                queryFn: () => projectsApi.listMembers(project.id),
                enabled: isOpen && !!user,
            })) || [],
    })

    const isLoading = userLoading || projectsLoading
    const isError = userError || projectsError
    const error = userErrorData || projectsErrorData

    const getRoleLabel = (role: string) => {
        switch (role) {
            case 'owner':
                return 'Владелец'
            case 'editor':
                return 'Редактор'
            case 'viewer':
                return 'Наблюдатель'
            default:
                return role
        }
    }

    return (
        <Modal isOpen={isOpen} onClose={onClose} title="Профиль пользователя" className="user-profile-modal">
            {isLoading && <Loading />}

            {isError && (
                <Error
                    message={
                        error && typeof error === 'object' && 'message' in error
                            ? String(error.message)
                            : 'Ошибка загрузки данных профиля'
                    }
                />
            )}

            {!isLoading && !isError && user && (
                <>
                    <div className="user-info-section">
                        <h3>Информация о пользователе</h3>
                        <div className="info-grid">
                            <InfoRow label="ID" value={<span className="mono">{user.id}</span>} />
                            <InfoRow label="Имя пользователя" value={user.username} />
                            <InfoRow label="Email" value={user.email} />
                            {user.created_at && (
                                <InfoRow
                                    label="Дата регистрации"
                                    value={format(new Date(user.created_at), 'dd MMM yyyy HH:mm')}
                                />
                            )}
                        </div>
                    </div>

                    <div className="user-projects-section">
                        <h3>Проекты</h3>
                        {projectsLoading && <Loading />}
                        {projectsError && (
                            <Error
                                message={
                                    projectsErrorData
                                        ? String(
                                            typeof projectsErrorData === 'object' && 'message' in projectsErrorData
                                                ? (projectsErrorData as { message: unknown }).message
                                                : projectsErrorData
                                        )
                                        : 'Ошибка загрузки проектов'
                                }
                            />
                        )}
                        {!projectsLoading && !projectsError && projectsData && (
                            <>
                                {projectsData.projects.length === 0 ? (
                                    <EmptyState message="У вас нет доступных проектов" />
                                ) : (
                                    <div className="projects-list">
                                        <table>
                                            <thead>
                                                <tr>
                                                    <th>Название</th>
                                                    <th>Описание</th>
                                                    <th>Роль</th>
                                                    <th>Создан</th>
                                                </tr>
                                            </thead>
                                            <tbody>
                                                {projectsData.projects.map((project, index) => {
                                                    // Определяем роль пользователя в проекте
                                                    const isOwner = user.id === project.owner_id
                                                    let role: string = 'viewer' // По умолчанию viewer

                                                    if (isOwner) {
                                                        role = 'owner'
                                                    } else {
                                                        // Пытаемся получить роль из списка участников
                                                        const membersData = memberQueries[index]?.data
                                                        if (membersData?.members) {
                                                            const member = membersData.members.find(
                                                                (m) => m.user_id === user.id
                                                            )
                                                            if (member) {
                                                                role = member.role
                                                            }
                                                        }
                                                    }

                                                    return (
                                                        <tr key={project.id}>
                                                            <td>
                                                                <strong>{project.name}</strong>
                                                            </td>
                                                            <td>
                                                                {project.description || (
                                                                    <span className="text-muted">Нет описания</span>
                                                                )}
                                                            </td>
                                                            <td>
                                                                <span className="badge badge-info">
                                                                    {getRoleLabel(role)}
                                                                </span>
                                                            </td>
                                                            <td>
                                                                {format(
                                                                    new Date(project.created_at),
                                                                    'dd MMM yyyy'
                                                                )}
                                                            </td>
                                                        </tr>
                                                    )
                                                })}
                                            </tbody>
                                        </table>
                                    </div>
                                )}
                            </>
                        )}
                    </div>

                    <div className="modal-actions">
                        <button
                            type="button"
                            className="btn btn-secondary"
                            onClick={onClose}
                        >
                            Закрыть
                        </button>
                    </div>
                </>
            )}
        </Modal>
    )
}

export default UserProfileModal

