import { useState, useEffect, useRef } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import Autocomplete from '@mui/material/Autocomplete'
import TextField from '@mui/material/TextField'
import CircularProgress from '@mui/material/CircularProgress'
import Chip from '@mui/material/Chip'
import { projectsApi, usersApi } from '../api/client'
import { authApi } from '../api/auth'
import { permissionsApi } from '../api/permissions'
import type { ProjectMemberAdd, ProjectMemberUpdate, UserSearchResult } from '../types'
import type { Role } from '../types/permissions'
import Modal from './Modal'
import PermissionGate from './PermissionGate'
import { Loading, Error, MaterialSelect } from './common'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'
import './CreateRunModal.scss'

// ---------------------------------------------------------------------------
// PermissionPicker (заглушка — будет заменена компонентом из задачи 6.3)
// ---------------------------------------------------------------------------

interface PermissionPickerProps {
    scope: 'project' | 'system'
    value: string[]
    onChange: (ids: string[]) => void
    disabled?: boolean
}

function PermissionPicker({ scope, value, onChange, disabled }: PermissionPickerProps) {
    const { data: allPermissions = [], isLoading } = useQuery({
        queryKey: ['permissions', 'list'],
        queryFn: () => permissionsApi.listPermissions(),
        staleTime: 60_000,
    })

    const scoped = allPermissions.filter((p) => p.scope === scope)

    const toggle = (id: string) => {
        if (value.includes(id)) {
            onChange(value.filter((v) => v !== id))
        } else {
            onChange([...value, id])
        }
    }

    if (isLoading) return <CircularProgress size={16} />
    if (scoped.length === 0) return <span className="text-muted">Нет доступных разрешений</span>

    return (
        <div className="permission-picker">
            {scoped.map((p) => (
                <label key={p.id} className="permission-picker__item">
                    <input
                        type="checkbox"
                        checked={value.includes(p.id)}
                        onChange={() => toggle(p.id)}
                        disabled={disabled}
                    />
                    <span className="permission-picker__name">{p.name}</span>
                    {p.description && (
                        <span className="permission-picker__desc text-muted"> — {p.description}</span>
                    )}
                </label>
            ))}
        </div>
    )
}

// ---------------------------------------------------------------------------
// MemberRolesModal — суб-модалка управления ролями одного участника
// ---------------------------------------------------------------------------

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
    const queryClient = useQueryClient()

    const { data: projectRoles = [], isLoading: rolesLoading } = useQuery({
        queryKey: ['projects', projectId, 'roles'],
        queryFn: () => permissionsApi.listProjectRoles(projectId),
        enabled: isOpen,
        staleTime: 30_000,
    })

    const grantMutation = useMutation({
        mutationFn: (roleId: string) =>
            permissionsApi.grantProjectRole(projectId, userId, roleId),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['projects', projectId, 'members'] })
            notifySuccess('Роль назначена')
        },
        onError: (err: any) => {
            const msg = err.response?.data?.error || err.message || 'Ошибка назначения роли'
            notifyError(msg)
        },
    })

    const revokeMutation = useMutation({
        mutationFn: (roleId: string) =>
            permissionsApi.revokeProjectRole(projectId, userId, roleId),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['projects', projectId, 'members'] })
            notifySuccess('Роль отозвана')
        },
        onError: (err: any) => {
            const msg = err.response?.data?.error || err.message || 'Ошибка отзыва роли'
            notifyError(msg)
        },
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

// ---------------------------------------------------------------------------
// CreateProjectRoleModal — мини-модалка создания кастомной роли
// ---------------------------------------------------------------------------

interface CreateProjectRoleModalProps {
    isOpen: boolean
    onClose: () => void
    projectId: string
}

function CreateProjectRoleModal({ isOpen, onClose, projectId }: CreateProjectRoleModalProps) {
    const queryClient = useQueryClient()
    const [name, setName] = useState('')
    const [description, setDescription] = useState('')
    const [permissionIds, setPermissionIds] = useState<string[]>([])

    const createMutation = useMutation({
        mutationFn: () =>
            permissionsApi.createProjectRole(projectId, {
                name,
                description: description || undefined,
                permissions: permissionIds,
            }),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['projects', projectId, 'roles'] })
            setName('')
            setDescription('')
            setPermissionIds([])
            notifySuccess('Роль создана')
            onClose()
        },
        onError: (err: any) => {
            const msg = err.response?.data?.error || err.message || 'Ошибка создания роли'
            notifyError(msg)
        },
    })

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        if (!name.trim()) {
            notifyError('Введите название роли')
            return
        }
        createMutation.mutate()
    }

    const handleClose = () => {
        if (!createMutation.isPending) {
            setName('')
            setDescription('')
            setPermissionIds([])
            onClose()
        }
    }

    return (
        <Modal
            isOpen={isOpen}
            onClose={handleClose}
            title="Создать роль проекта"
            disabled={createMutation.isPending}
            className="create-project-role-modal"
        >
            <div className="modal-form">
                <form onSubmit={handleSubmit}>
                    <div className="form-group">
                        <label htmlFor="role_name">
                            Название <span className="required">*</span>
                        </label>
                        <input
                            id="role_name"
                            type="text"
                            className="form-control"
                            value={name}
                            onChange={(e) => setName(e.target.value)}
                            disabled={createMutation.isPending}
                            placeholder="Например: reviewer"
                            required
                        />
                    </div>
                    <div className="form-group">
                        <label htmlFor="role_description">Описание</label>
                        <input
                            id="role_description"
                            type="text"
                            className="form-control"
                            value={description}
                            onChange={(e) => setDescription(e.target.value)}
                            disabled={createMutation.isPending}
                            placeholder="Необязательно"
                        />
                    </div>
                    <div className="form-group">
                        <label>Разрешения</label>
                        <PermissionPicker
                            scope="project"
                            value={permissionIds}
                            onChange={setPermissionIds}
                            disabled={createMutation.isPending}
                        />
                    </div>
                    <div className="modal-actions">
                        <button
                            type="submit"
                            className="btn btn-primary"
                            disabled={createMutation.isPending || !name.trim()}
                        >
                            {createMutation.isPending ? 'Создание...' : 'Создать роль'}
                        </button>
                        <button
                            type="button"
                            className="btn btn-secondary"
                            onClick={handleClose}
                            disabled={createMutation.isPending}
                        >
                            Отмена
                        </button>
                    </div>
                </form>
            </div>
        </Modal>
    )
}

// ---------------------------------------------------------------------------
// ProjectRolesSection — секция управления ролями проекта
// ---------------------------------------------------------------------------

interface ProjectRolesSectionProps {
    projectId: string
}

function ProjectRolesSection({ projectId }: ProjectRolesSectionProps) {
    const queryClient = useQueryClient()
    const [isCreateModalOpen, setIsCreateModalOpen] = useState(false)
    const [isExpanded, setIsExpanded] = useState(false)  // collapsible section

    const { data: projectRoles = [], isLoading } = useQuery({
        queryKey: ['projects', projectId, 'roles'],
        queryFn: () => permissionsApi.listProjectRoles(projectId),
        staleTime: 30_000,
    })

    const deleteMutation = useMutation({
        mutationFn: (roleId: string) => permissionsApi.deleteProjectRole(projectId, roleId),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['projects', projectId, 'roles'] })
            notifySuccess('Роль удалена')
        },
        onError: (err: any) => {
            const msg = err.response?.data?.error || err.message || 'Ошибка удаления роли'
            notifyError(msg)
        },
    })

    const handleDeleteRole = (role: Role) => {
        if (!confirm(`Удалить роль "${role.name}"?`)) return
        deleteMutation.mutate(role.id)
    }

    return (
        <PermissionGate permission="project.roles.manage">
            <div className="project-roles-section">
                <div style={{ border: '1px solid var(--border-color, #e0e0e0)', borderRadius: 4 }}>
                    <button
                        type="button"
                        className="btn btn-ghost"
                        style={{ width: '100%', textAlign: 'left', fontWeight: 600, padding: '8px 12px', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}
                        onClick={() => setIsExpanded(v => !v)}
                    >
                        <span>Роли проекта</span>
                        <span>{isExpanded ? '▲' : '▼'}</span>
                    </button>
                    {isExpanded && (
                        <div style={{ padding: '0 12px 12px' }}>
                            {isLoading && <Loading />}
                            {!isLoading && (
                                <>
                                    {projectRoles.length === 0 ? (
                                        <p className="empty-message">Нет ролей</p>
                                    ) : (
                                        <table style={{ width: '100%', marginBottom: 12 }}>
                                            <thead>
                                                <tr>
                                                    <th>Название</th>
                                                    <th>Описание</th>
                                                    <th>Тип</th>
                                                    <th>Действия</th>
                                                </tr>
                                            </thead>
                                            <tbody>
                                                {projectRoles.map((role) => (
                                                    <tr key={role.id}>
                                                        <td>{role.name}</td>
                                                        <td className="text-muted">{role.description || '—'}</td>
                                                        <td>
                                                            {role.is_builtin ? (
                                                                <span className="badge badge-secondary">Встроенная</span>
                                                            ) : (
                                                                <span className="badge">Кастомная</span>
                                                            )}
                                                        </td>
                                                        <td>
                                                            {role.is_builtin ? (
                                                                <span className="text-muted">—</span>
                                                            ) : (
                                                                <button
                                                                    type="button"
                                                                    className="btn btn-danger btn-sm"
                                                                    onClick={() => handleDeleteRole(role)}
                                                                    disabled={deleteMutation.isPending}
                                                                >
                                                                    Удалить
                                                                </button>
                                                            )}
                                                        </td>
                                                    </tr>
                                                ))}
                                            </tbody>
                                        </table>
                                    )}
                                    <button
                                        type="button"
                                        className="btn btn-secondary btn-sm"
                                        onClick={() => setIsCreateModalOpen(true)}
                                    >
                                        + Создать роль
                                    </button>
                                </>
                            )}
                        </div>
                    )}
                </div>

                <CreateProjectRoleModal
                    isOpen={isCreateModalOpen}
                    onClose={() => setIsCreateModalOpen(false)}
                    projectId={projectId}
                />
            </div>
        </PermissionGate>
    )
}

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
    const queryClient = useQueryClient()
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
    const addMemberMutation = useMutation({
        mutationFn: (data: ProjectMemberAdd) => projectsApi.addMember(projectId, data),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['projects', projectId, 'members'] })
            setSelectedUser(null)
            setUserSearchInput('')
            setDebouncedQuery('')
            setNewMemberRoleId('')
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

    // Мутация для изменения роли участника (legacy)
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
        <>
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
                                                    <th>Роли</th>
                                                    {isOwner && <th>Действия</th>}
                                                </tr>
                                            </thead>
                                            <tbody>
                                                {membersData.members.map((member) => {
                                                    const isCurrentUser = member.user_id === currentUserId
                                                    const isMemberOwner = member.user_id === projectOwnerId
                                                    const canEdit = isOwner && !isMemberOwner

                                                    // Все роли из projectRoles, у которых name совпадает с member.role
                                                    // или которые были назначены через RBAC v2
                                                    // Для отображения показываем legacy role как тег + любые проектные роли
                                                    const legacyRoleLabel = getRoleLabel(member.role)

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
                                                                <div style={{ display: 'flex', flexWrap: 'wrap', gap: 4, alignItems: 'center' }}>
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
                                                                        <Chip
                                                                            label={legacyRoleLabel}
                                                                            size="small"
                                                                            variant="outlined"
                                                                        />
                                                                    )}
                                                                </div>
                                                            </td>
                                                            {isOwner && (
                                                                <td>
                                                                    <div style={{ display: 'flex', gap: 4 }}>
                                                                        {canEdit ? (
                                                                            <>
                                                                                <button
                                                                                    type="button"
                                                                                    className="btn btn-secondary btn-sm"
                                                                                    onClick={() =>
                                                                                        setManagingMember({
                                                                                            userId: member.user_id,
                                                                                            username: member.username || member.user_id,
                                                                                            assignedRoleIds: projectRoles
                                                                                                .filter((r) => r.name === member.role)
                                                                                                .map((r) => r.id),
                                                                                        })
                                                                                    }
                                                                                    disabled={isPending}
                                                                                >
                                                                                    Управление ролями
                                                                                </button>
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
                                                                            </>
                                                                        ) : (
                                                                            <span className="text-muted">
                                                                                Нельзя удалить
                                                                            </span>
                                                                        )}
                                                                    </div>
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
                                            {projectRoles.length > 0 ? (
                                                <MaterialSelect
                                                    id="new_member_role"
                                                    value={newMemberRoleId}
                                                    onChange={(value) => setNewMemberRoleId(value)}
                                                    disabled={isPending}
                                                >
                                                    {projectRoles.map((role) => (
                                                        <option key={role.id} value={role.id}>
                                                            {role.name}
                                                            {role.is_builtin ? '' : ' (кастомная)'}
                                                        </option>
                                                    ))}
                                                </MaterialSelect>
                                            ) : (
                                                <MaterialSelect
                                                    id="new_member_role"
                                                    value="viewer"
                                                    onChange={() => {}}
                                                    disabled={isPending}
                                                >
                                                    <option value="viewer">Наблюдатель</option>
                                                    <option value="editor">Редактор</option>
                                                    <option value="owner">Владелец</option>
                                                </MaterialSelect>
                                            )}
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
