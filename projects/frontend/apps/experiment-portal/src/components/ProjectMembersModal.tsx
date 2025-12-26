import { useState, useEffect } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { projectsApi } from '../api/client'
import { authApi } from '../api/auth'
import type { ProjectMemberAdd, ProjectMemberUpdate } from '../types'
import Modal from './Modal'
import { Loading, Error } from './common'
import './CreateRunModal.css'

interface ProjectMembersModalProps {
    isOpen: boolean
    onClose: () => void
    projectId: string
    projectOwnerId: string
}

function ProjectMembersModal({ isOpen, onClose, projectId, projectOwnerId }: ProjectMembersModalProps) {
    const queryClient = useQueryClient()
    const [currentUserId, setCurrentUserId] = useState<string | null>(null)
    const [isOwner, setIsOwner] = useState(false)
    const [newMemberUserId, setNewMemberUserId] = useState('')
    const [newMemberRole, setNewMemberRole] = useState<'owner' | 'editor' | 'viewer'>('viewer')
    const [error, setError] = useState<string | null>(null)

    // Получаем текущего пользователя
    const { data: currentUser } = useQuery({
        queryKey: ['auth', 'me'],
        queryFn: () => authApi.me(),
        enabled: isOpen,
    })

    // Получаем список участников проекта
    const {
        data: membersData,
        isLoading,
        isError,
        error: membersError,
    } = useQuery({
        queryKey: ['projects', projectId, 'members'],
        queryFn: () => projectsApi.listMembers(projectId),
        enabled: isOpen,
    })

    // Определяем роль текущего пользователя
    useEffect(() => {
        if (currentUser) {
            setCurrentUserId(currentUser.id)
            // Проверяем, является ли пользователь владельцем проекта
            const isProjectOwner = currentUser.id === projectOwnerId
            setIsOwner(isProjectOwner)

            // Если не owner, проверяем роль в списке участников
            if (!isProjectOwner && membersData?.members) {
                const member = membersData.members.find((m) => m.user_id === currentUser.id)
                if (member?.role === 'owner') {
                    setIsOwner(true)
                }
            }
        }
    }, [currentUser, projectOwnerId, membersData])

    // Мутация для добавления участника
    const addMemberMutation = useMutation({
        mutationFn: (data: ProjectMemberAdd) => projectsApi.addMember(projectId, data),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['projects', projectId, 'members'] })
            setNewMemberUserId('')
            setNewMemberRole('viewer')
            setError(null)
        },
        onError: (err: any) => {
            setError(err.response?.data?.error || err.message || 'Ошибка добавления участника')
        },
    })

    // Мутация для удаления участника
    const removeMemberMutation = useMutation({
        mutationFn: (userId: string) => projectsApi.removeMember(projectId, userId),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['projects', projectId, 'members'] })
            queryClient.invalidateQueries({ queryKey: ['projects'] })
            setError(null)
        },
        onError: (err: any) => {
            setError(err.response?.data?.error || err.message || 'Ошибка удаления участника')
        },
    })

    // Мутация для изменения роли участника
    const updateRoleMutation = useMutation({
        mutationFn: ({ userId, data }: { userId: string; data: ProjectMemberUpdate }) =>
            projectsApi.updateMemberRole(projectId, userId, data),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['projects', projectId, 'members'] })
            setError(null)
        },
        onError: (err: any) => {
            setError(err.response?.data?.error || err.message || 'Ошибка изменения роли')
        },
    })

    const handleAddMember = (e: React.FormEvent) => {
        e.preventDefault()
        setError(null)

        if (!newMemberUserId.trim()) {
            setError('Введите ID пользователя')
            return
        }

        addMemberMutation.mutate({
            user_id: newMemberUserId.trim(),
            role: newMemberRole,
        })
    }

    const handleRemoveMember = (userId: string, username?: string | null) => {
        if (!confirm(`Удалить участника ${username || userId} из проекта?`)) {
            return
        }
        removeMemberMutation.mutate(userId)
    }

    const handleRoleChange = (userId: string, newRole: 'owner' | 'editor' | 'viewer') => {
        updateRoleMutation.mutate({
            userId,
            data: { role: newRole },
        })
    }

    const handleClose = () => {
        if (!addMemberMutation.isPending && !removeMemberMutation.isPending && !updateRoleMutation.isPending) {
            setNewMemberUserId('')
            setNewMemberRole('viewer')
            setError(null)
            onClose()
        }
    }

    const getRoleLabel = (role: string) => {
        const labels: Record<string, string> = {
            owner: 'Владелец',
            editor: 'Редактор',
            viewer: 'Наблюдатель',
        }
        return labels[role] || role
    }

    const isPending = addMemberMutation.isPending || removeMemberMutation.isPending || updateRoleMutation.isPending

    return (
        <Modal
            isOpen={isOpen}
            onClose={handleClose}
            title="Участники проекта"
            disabled={isPending}
            className="project-members-modal"
        >
            <div className="modal-form">
                {error && <div className="error">{error}</div>}

                {isLoading && <Loading />}
                {isError && (
                    <Error
                        message={
                            membersError instanceof Error
                                ? membersError.message
                                : 'Ошибка загрузки участников'
                        }
                    />
                )}

                {membersData && (
                    <>
                        <div className="members-list">
                            <h3>Участники ({membersData.members.length})</h3>
                            {membersData.members.length === 0 ? (
                                <p className="empty-message">Нет участников</p>
                            ) : (
                                <div className="members-table">
                                    <table>
                                        <thead>
                                            <tr>
                                                <th>Пользователь</th>
                                                <th>Роль</th>
                                                {isOwner && <th>Действия</th>}
                                            </tr>
                                        </thead>
                                        <tbody>
                                            {membersData.members.map((member) => {
                                                const isCurrentUser = member.user_id === currentUserId
                                                const isMemberOwner = member.user_id === projectOwnerId
                                                const canEdit = isOwner && !isMemberOwner

                                                return (
                                                    <tr key={member.user_id}>
                                                        <td>
                                                            {member.username || member.user_id}
                                                            {isCurrentUser && (
                                                                <span className="badge">Вы</span>
                                                            )}
                                                            {isMemberOwner && (
                                                                <span className="badge badge-primary">
                                                                    Владелец проекта
                                                                </span>
                                                            )}
                                                        </td>
                                                        <td>
                                                            {canEdit ? (
                                                                <select
                                                                    value={member.role}
                                                                    onChange={(e) =>
                                                                        handleRoleChange(
                                                                            member.user_id,
                                                                            e.target
                                                                                .value as 'owner' | 'editor' | 'viewer'
                                                                        )
                                                                    }
                                                                    disabled={isPending}
                                                                >
                                                                    <option value="viewer">Наблюдатель</option>
                                                                    <option value="editor">Редактор</option>
                                                                    <option value="owner">Владелец</option>
                                                                </select>
                                                            ) : (
                                                                <span>{getRoleLabel(member.role)}</span>
                                                            )}
                                                        </td>
                                                        {isOwner && (
                                                            <td>
                                                                {canEdit ? (
                                                                    <button
                                                                        type="button"
                                                                        className="btn btn-danger btn-sm"
                                                                        onClick={() =>
                                                                            handleRemoveMember(
                                                                                member.user_id,
                                                                                member.username
                                                                            )
                                                                        }
                                                                        disabled={isPending}
                                                                    >
                                                                        Удалить
                                                                    </button>
                                                                ) : (
                                                                    <span className="text-muted">
                                                                        Нельзя удалить
                                                                    </span>
                                                                )}
                                                            </td>
                                                        )}
                                                    </tr>
                                                )
                                            })}
                                        </tbody>
                                    </table>
                                </div>
                            )}
                        </div>

                        {isOwner && (
                            <div className="add-member-form">
                                <h3>Добавить участника</h3>
                                <form onSubmit={handleAddMember}>
                                    <div className="form-group">
                                        <label htmlFor="new_member_user_id">
                                            ID пользователя <span className="required">*</span>
                                        </label>
                                        <input
                                            id="new_member_user_id"
                                            type="text"
                                            value={newMemberUserId}
                                            onChange={(e) => setNewMemberUserId(e.target.value)}
                                            placeholder="Введите UUID пользователя"
                                            required
                                            disabled={isPending}
                                        />
                                    </div>

                                    <div className="form-group">
                                        <label htmlFor="new_member_role">Роль</label>
                                        <select
                                            id="new_member_role"
                                            value={newMemberRole}
                                            onChange={(e) =>
                                                setNewMemberRole(
                                                    e.target.value as 'owner' | 'editor' | 'viewer'
                                                )
                                            }
                                            disabled={isPending}
                                        >
                                            <option value="viewer">Наблюдатель</option>
                                            <option value="editor">Редактор</option>
                                            <option value="owner">Владелец</option>
                                        </select>
                                    </div>

                                    <button
                                        type="submit"
                                        className="btn btn-primary"
                                        disabled={isPending || !newMemberUserId.trim()}
                                    >
                                        {addMemberMutation.isPending ? 'Добавление...' : 'Добавить участника'}
                                    </button>
                                </form>
                            </div>
                        )}
                    </>
                )}

                <div className="modal-actions">
                    <button
                        type="button"
                        className="btn btn-secondary"
                        onClick={handleClose}
                        disabled={isPending}
                    >
                        Закрыть
                    </button>
                </div>
            </div>
        </Modal>
    )
}

export default ProjectMembersModal

