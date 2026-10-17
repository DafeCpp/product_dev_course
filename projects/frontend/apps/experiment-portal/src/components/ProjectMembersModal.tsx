import { useState, useEffect, useRef } from 'react'
import { useQuery } from '@tanstack/react-query'
import { useApiMutation } from '../hooks/useApiMutation'
import { projectsApi, usersApi } from '../api/client'
import { authApi } from '../api/auth'
import { permissionsApi } from '../api/permissions'
import type { ProjectMemberAdd, ProjectMemberUpdate, UserSearchResult } from '../types'
import Modal from './Modal'
import { Loading, Error } from './common'
import { getApiErrorMessage } from '../utils/apiError'
import MembersTable from './project-members/MembersTable'
import AddMemberForm from './project-members/AddMemberForm'
import MemberRolesModal from './project-members/MemberRolesModal'
import ProjectRolesSection from './project-members/ProjectRolesSection'
import './CreateRunModal.scss'

// ---------------------------------------------------------------------------
// ProjectMembersModal — основная модалка
// ---------------------------------------------------------------------------

interface ProjectMembersModalProps {
    isOpen: boolean
    onClose: () => void
    projectId: string
    projectOwnerId: string
}

function ProjectMembersModal({ isOpen, onClose, projectId, projectOwnerId }: ProjectMembersModalProps) {
    const [currentUserId, setCurrentUserId] = useState<string | null>(null)
    const [isOwner, setIsOwner] = useState(false)
    const [selectedUser, setSelectedUser] = useState<UserSearchResult | null>(null)
    const [userSearchInput, setUserSearchInput] = useState('')
    const [newMemberRoleId, setNewMemberRoleId] = useState<string>('')
    const [error, setError] = useState<string | null>(null)
    const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null)
    const [debouncedQuery, setDebouncedQuery] = useState('')

    // Суб-модалка управления ролями
    const [managingMember, setManagingMember] = useState<{
        userId: string
        username: string
        assignedRoleIds: string[]
    } | null>(null)

    // Debounce user search input 300ms, min 2 chars
    useEffect(() => {
        if (debounceRef.current) clearTimeout(debounceRef.current)
        if (userSearchInput.length >= 2) {
            debounceRef.current = setTimeout(() => {
                setDebouncedQuery(userSearchInput)
            }, 300)
        } else {
            setDebouncedQuery('')
        }
        return () => {
            if (debounceRef.current) clearTimeout(debounceRef.current)
        }
    }, [userSearchInput])

    // Search users
    const {
        data: usersSearchData,
        isFetching: isSearchingUsers,
    } = useQuery({
        queryKey: ['users', 'search', debouncedQuery, projectId],
        queryFn: () => usersApi.search({ q: debouncedQuery, exclude_project_id: projectId }),
        enabled: debouncedQuery.length >= 2,
        staleTime: 30_000,
    })

    const userOptions: UserSearchResult[] = usersSearchData?.users ?? []

    // Текущий пользователь
    const { data: currentUser } = useQuery({
        queryKey: ['auth', 'me'],
        queryFn: () => authApi.me(),
        enabled: isOpen,
    })

    // Список участников проекта
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

    // Список проектных ролей для формы добавления
    const { data: projectRoles = [] } = useQuery({
        queryKey: ['projects', projectId, 'roles'],
        queryFn: () => permissionsApi.listProjectRoles(projectId),
        enabled: isOpen,
        staleTime: 30_000,
    })

    // Устанавливаем дефолтную роль когда загрузятся роли
    useEffect(() => {
        if (projectRoles.length > 0 && !newMemberRoleId) {
            const viewerRole = projectRoles.find((r) => r.name === 'viewer')
            setNewMemberRoleId(viewerRole?.id ?? projectRoles[0].id)
        }
    }, [projectRoles, newMemberRoleId])

    // Определяем роль текущего пользователя
    useEffect(() => {
        if (currentUser) {
            setCurrentUserId(currentUser.id)
            const isProjectOwner = currentUser.id === projectOwnerId
            setIsOwner(isProjectOwner)

            if (!isProjectOwner && membersData?.members) {
                const member = membersData.members.find((m) => m.user_id === currentUser.id)
                if (member?.role === 'owner') {
                    setIsOwner(true)
                }
            }
        }
    }, [currentUser, projectOwnerId, membersData])

    // Мутация для добавления участника
    const addMemberMutation = useApiMutation<unknown, ProjectMemberAdd>({
        mutationFn: (data) => projectsApi.addMember(projectId, data),
        invalidateKeys: [['projects', projectId, 'members']],
        successMessage: 'Участник добавлен',
        errorFallback: 'Ошибка добавления участника',
        onSuccess: () => { setSelectedUser(null); setUserSearchInput(''); setDebouncedQuery(''); setNewMemberRoleId(''); setError(null) },
        notifyOnError: false,
        onError: (_err, _vars, message) => setError(message),
    })

    // Мутация для удаления участника
    const removeMemberMutation = useApiMutation<unknown, string>({
        mutationFn: (userId) => projectsApi.removeMember(projectId, userId),
        invalidateKeys: [['projects', projectId, 'members'], ['projects']],
        successMessage: 'Участник удалён',
        errorFallback: 'Ошибка удаления участника',
        onSuccess: () => setError(null),
        notifyOnError: false,
        onError: (_err, _vars, message) => setError(message),
    })

    // Мутация для изменения роли участника (legacy)
    const updateRoleMutation = useApiMutation<unknown, { userId: string; data: ProjectMemberUpdate }>({
        mutationFn: ({ userId, data }) => projectsApi.updateMemberRole(projectId, userId, data),
        invalidateKeys: [['projects', projectId, 'members']],
        successMessage: 'Роль обновлена',
        errorFallback: 'Ошибка изменения роли',
        onSuccess: () => setError(null),
        notifyOnError: false,
        onError: (_err, _vars, message) => setError(message),
    })

    const handleAddMember = (e: React.FormEvent) => {
        e.preventDefault()
        setError(null)

        if (!selectedUser) {
            setError('Выберите пользователя из списка')
            return
        }

        // Определяем legacy-роль для обратной совместимости
        // Если выбранная роль совпадает со встроенной по имени — используем её name,
        // иначе дефолт viewer
        const selectedRole = projectRoles.find((r) => r.id === newMemberRoleId)
        const legacyRole = (
            selectedRole && ['owner', 'editor', 'viewer'].includes(selectedRole.name)
                ? selectedRole.name
                : 'viewer'
        ) as 'owner' | 'editor' | 'viewer'

        addMemberMutation.mutate({
            user_id: selectedUser.id,
            role: legacyRole,
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
            setSelectedUser(null)
            setUserSearchInput('')
            setDebouncedQuery('')
            setNewMemberRoleId('')
            setError(null)
            onClose()
        }
    }

    const isPending = addMemberMutation.isPending || removeMemberMutation.isPending || updateRoleMutation.isPending

    return (
        <>
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
                        <Error message={getApiErrorMessage(membersError, 'Ошибка загрузки участников')} />
                    )}

                    {membersData && (
                        <>
                            <div className="members-list">
                                <h3>Участники ({membersData.members.length})</h3>
                                <MembersTable
                                    members={membersData.members}
                                    projectRoles={projectRoles}
                                    currentUserId={currentUserId}
                                    projectOwnerId={projectOwnerId}
                                    isOwner={isOwner}
                                    isPending={isPending}
                                    onRoleChange={handleRoleChange}
                                    onManageRoles={setManagingMember}
                                    onRemoveMember={handleRemoveMember}
                                />
                            </div>

                            {isOwner && (
                                <AddMemberForm
                                    selectedUser={selectedUser}
                                    userSearchInput={userSearchInput}
                                    userOptions={userOptions}
                                    isSearchingUsers={isSearchingUsers}
                                    debouncedQuery={debouncedQuery}
                                    newMemberRoleId={newMemberRoleId}
                                    projectRoles={projectRoles}
                                    isPending={isPending}
                                    isAddPending={addMemberMutation.isPending}
                                    onUserInputChange={setUserSearchInput}
                                    onUserSelect={setSelectedUser}
                                    onRoleChange={setNewMemberRoleId}
                                    onSubmit={handleAddMember}
                                />
                            )}

                            <ProjectRolesSection projectId={projectId} />
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

            {managingMember && (
                <MemberRolesModal
                    isOpen={true}
                    onClose={() => setManagingMember(null)}
                    projectId={projectId}
                    userId={managingMember.userId}
                    username={managingMember.username}
                    assignedRoleIds={managingMember.assignedRoleIds}
                />
            )}
        </>
    )
}

export default ProjectMembersModal
