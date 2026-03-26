import { useState, useEffect, useRef } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import Autocomplete from '@mui/material/Autocomplete'
import TextField from '@mui/material/TextField'
import CircularProgress from '@mui/material/CircularProgress'
import { projectsApi, usersApi } from '../api/client'
import { authApi } from '../api/auth'
import type { ProjectMemberAdd, ProjectMemberUpdate, UserSearchResult } from '../types'
import Modal from './Modal'
import { Loading, Error, MaterialSelect } from './common'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'
import './CreateRunModal.scss'

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
    const [selectedUser, setSelectedUser] = useState<UserSearchResult | null>(null)
    const [userSearchInput, setUserSearchInput] = useState('')
    const [newMemberRole, setNewMemberRole] = useState<'owner' | 'editor' | 'viewer'>('viewer')
    const [error, setError] = useState<string | null>(null)
    const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null)
    const [debouncedQuery, setDebouncedQuery] = useState('')

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
    const addMemberMutation = useMutation({
        mutationFn: (data: ProjectMemberAdd) => projectsApi.addMember(projectId, data),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['projects', projectId, 'members'] })
            setSelectedUser(null)
            setUserSearchInput('')
            setDebouncedQuery('')
            setNewMemberRole('viewer')
            setError(null)
            notifySuccess('Участник добавлен')
        },
        onError: (err: any) => {
            const msg = err.response?.data?.error || err.message || 'Ошибка добавления участника'
            setError(msg)
            notifyError(msg)
        },
    })

    // Мутация для удаления участника
    const removeMemberMutation = useMutation({
        mutationFn: (userId: string) => projectsApi.removeMember(projectId, userId),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['projects', projectId, 'members'] })
            queryClient.invalidateQueries({ queryKey: ['projects'] })
            setError(null)
            notifySuccess('Участник удалён')
        },
        onError: (err: any) => {
            const msg = err.response?.data?.error || err.message || 'Ошибка удаления участника'
            setError(msg)
            notifyError(msg)
        },
    })

    // Мутация для изменения роли участника
    const updateRoleMutation = useMutation({
        mutationFn: ({ userId, data }: { userId: string; data: ProjectMemberUpdate }) =>
            projectsApi.updateMemberRole(projectId, userId, data),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['projects', projectId, 'members'] })
            setError(null)
            notifySuccess('Роль обновлена')
        },
        onError: (err: any) => {
            const msg = err.response?.data?.error || err.message || 'Ошибка изменения роли'
            setError(msg)
            notifyError(msg)
        },
    })

    const handleAddMember = (e: React.FormEvent) => {
        e.preventDefault()
        setError(null)

        if (!selectedUser) {
            const msg = 'Выберите пользователя из списка'
            setError(msg)
            notifyError(msg)
            return
        }

        addMemberMutation.mutate({
            user_id: selectedUser.id,
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
            setSelectedUser(null)
            setUserSearchInput('')
            setDebouncedQuery('')
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
                {IS_TEST && error && <div className="error">{error}</div>}

                {isLoading && <Loading />}
                {IS_TEST && isError && (
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
                                                                <MaterialSelect
                                                                    id={`member_role_${member.user_id}`}
                                                                    value={member.role}
                                                                    onChange={(value) =>
                                                                        handleRoleChange(
                                                                            member.user_id,
                                                                            value as 'owner' | 'editor' | 'viewer'
                                                                        )
                                                                    }
                                                                    disabled={isPending}
                                                                    className="md-select--compact"
                                                                >
                                                                    <option value="viewer">Наблюдатель</option>
                                                                    <option value="editor">Редактор</option>
                                                                    <option value="owner">Владелец</option>
                                                                </MaterialSelect>
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
                                            Пользователь <span className="required">*</span>
                                        </label>
                                        <Autocomplete<UserSearchResult>
                                            id="new_member_user_id"
                                            options={userOptions}
                                            value={selectedUser}
                                            inputValue={userSearchInput}
                                            onInputChange={(_event, value) => {
                                                setUserSearchInput(value)
                                                if (!value) setSelectedUser(null)
                                            }}
                                            onChange={(_event, value) => setSelectedUser(value)}
                                            getOptionLabel={(option) => option.username}
                                            isOptionEqualToValue={(option, value) => option.id === value.id}
                                            renderOption={(props, option) => (
                                                <li {...props} key={option.id}>
                                                    <span style={{ fontWeight: 500 }}>{option.username}</span>
                                                    <span style={{ marginLeft: 8, color: '#888', fontSize: '0.85em' }}>
                                                        {option.email}
                                                    </span>
                                                </li>
                                            )}
                                            loading={isSearchingUsers}
                                            noOptionsText={
                                                debouncedQuery.length < 2
                                                    ? 'Введите минимум 2 символа'
                                                    : 'Пользователи не найдены'
                                            }
                                            disabled={isPending}
                                            renderInput={(params) => (
                                                <TextField
                                                    {...params}
                                                    placeholder="Поиск по имени или email"
                                                    size="small"
                                                    InputProps={{
                                                        ...params.InputProps,
                                                        endAdornment: (
                                                            <>
                                                                {isSearchingUsers ? (
                                                                    <CircularProgress color="inherit" size={16} />
                                                                ) : null}
                                                                {params.InputProps.endAdornment}
                                                            </>
                                                        ),
                                                    }}
                                                />
                                            )}
                                            filterOptions={(x) => x}
                                        />
                                    </div>

                                    <div className="form-group">
                                        <label htmlFor="new_member_role">Роль</label>
                                        <MaterialSelect
                                            id="new_member_role"
                                            value={newMemberRole}
                                            onChange={(value) =>
                                                setNewMemberRole(value as 'owner' | 'editor' | 'viewer')
                                            }
                                            disabled={isPending}
                                        >
                                            <option value="viewer">Наблюдатель</option>
                                            <option value="editor">Редактор</option>
                                            <option value="owner">Владелец</option>
                                        </MaterialSelect>
                                    </div>

                                    <button
                                        type="submit"
                                        className="btn btn-primary"
                                        disabled={isPending || !selectedUser}
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
