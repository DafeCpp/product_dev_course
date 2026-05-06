import { useQuery } from '@tanstack/react-query'
import { useApiMutation } from '../../hooks/useApiMutation'
import { permissionsApi } from '../../api/permissions'
import Modal from '../Modal'
import { Loading } from '../common'

interface MemberRolesModalProps {
    isOpen: boolean
    onClose: () => void
    projectId: string
    userId: string
    username: string
    /** Роли, уже назначенные участнику (из legacy member.role + project roles) */
    assignedRoleIds: string[]
}

function MemberRolesModal({
    isOpen,
    onClose,
    projectId,
    userId,
    username,
    assignedRoleIds,
}: MemberRolesModalProps) {
    const { data: projectRoles = [], isLoading: rolesLoading } = useQuery({
        queryKey: ['projects', projectId, 'roles'],
        queryFn: () => permissionsApi.listProjectRoles(projectId),
        enabled: isOpen,
        staleTime: 30_000,
    })

    const grantMutation = useApiMutation<unknown, string>({
        mutationFn: (roleId) => permissionsApi.grantProjectRole(projectId, userId, roleId),
        invalidateKeys: [['projects', projectId, 'members']],
        successMessage: 'Роль назначена',
        errorFallback: 'Ошибка назначения роли',
    })

    const revokeMutation = useApiMutation<unknown, string>({
        mutationFn: (roleId) => permissionsApi.revokeProjectRole(projectId, userId, roleId),
        invalidateKeys: [['projects', projectId, 'members']],
        successMessage: 'Роль отозвана',
        errorFallback: 'Ошибка отзыва роли',
    })

    const isPending = grantMutation.isPending || revokeMutation.isPending

    return (
        <Modal
            isOpen={isOpen}
            onClose={onClose}
            title={`Роли участника: ${username}`}
            disabled={isPending}
            className="member-roles-modal"
        >
            <div className="modal-form">
                {rolesLoading && <Loading />}
                {!rolesLoading && projectRoles.length === 0 && (
                    <p className="empty-message">Нет доступных ролей</p>
                )}
                {!rolesLoading && projectRoles.length > 0 && (
                    <div className="member-roles-list">
                        <table>
                            <thead>
                                <tr>
                                    <th>Роль</th>
                                    <th>Описание</th>
                                    <th>Действие</th>
                                </tr>
                            </thead>
                            <tbody>
                                {projectRoles.map((role) => {
                                    const assigned = assignedRoleIds.includes(role.id)
                                    return (
                                        <tr key={role.id}>
                                            <td>
                                                <span>{role.name}</span>
                                                {role.is_builtin && (
                                                    <span className="badge badge-secondary" style={{ marginLeft: 6 }}>
                                                        Встроенная
                                                    </span>
                                                )}
                                            </td>
                                            <td className="text-muted">{role.description || '—'}</td>
                                            <td>
                                                {assigned ? (
                                                    <button
                                                        type="button"
                                                        className="btn btn-danger btn-sm"
                                                        onClick={() => revokeMutation.mutate(role.id)}
                                                        disabled={isPending}
                                                    >
                                                        Отозвать
                                                    </button>
                                                ) : (
                                                    <button
                                                        type="button"
                                                        className="btn btn-secondary btn-sm"
                                                        onClick={() => grantMutation.mutate(role.id)}
                                                        disabled={isPending}
                                                    >
                                                        Назначить
                                                    </button>
                                                )}
                                            </td>
                                        </tr>
                                    )
                                })}
                            </tbody>
                        </table>
                    </div>
                )}
                <div className="modal-actions">
                    <button
                        type="button"
                        className="btn btn-secondary"
                        onClick={onClose}
                        disabled={isPending}
                    >
                        Закрыть
                    </button>
                </div>
            </div>
        </Modal>
    )
}

export default MemberRolesModal
