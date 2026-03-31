import { useState, useEffect, useRef } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { format } from 'date-fns'
import {
    Button,
    TextField,
    Typography,
    Box,
    Chip,
    List,
    ListItem,
    ListItemText,
} from '@mui/material'
import { authApi } from '../api/auth'
import { permissionsApi } from '../api/permissions'
import type { AdminUser, AdminInviteToken } from '../types'
import type { Role } from '../types/permissions'
import { Loading, Error as ErrorComponent, EmptyState } from '../components/common'
import { notifyError, notifySuccess } from '../utils/notify'
import PermissionGate from '../components/PermissionGate'
import UserRolesModal from '../components/UserRolesModal'
import './AdminUsers.scss'

type Tab = 'users' | 'invites' | 'system-roles'

function AdminUsers() {
    const queryClient = useQueryClient()
    const [tab, setTab] = useState<Tab>('users')

    // --- Users ---
    const [search, setSearch] = useState('')
    const [debouncedSearch, setDebouncedSearch] = useState('')
    const searchTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null)
    const [filterActive, setFilterActive] = useState<'all' | 'active' | 'inactive'>('all')
    const [resetResult, setResetResult] = useState<{ userId: string; password: string } | null>(null)

    useEffect(() => {
        if (searchTimerRef.current) clearTimeout(searchTimerRef.current)
        searchTimerRef.current = setTimeout(() => setDebouncedSearch(search), 300)
        return () => { if (searchTimerRef.current) clearTimeout(searchTimerRef.current) }
    }, [search])

    const usersQuery = useQuery({
        queryKey: ['admin', 'users', debouncedSearch, filterActive],
        queryFn: () =>
            authApi.adminListUsers({
                search: debouncedSearch || undefined,
                is_active: filterActive === 'all' ? undefined : filterActive === 'active',
            }),
        retry: 1,
    })

    const updateUserMutation = useMutation({
        mutationFn: (args: { userId: string; data: { is_active?: boolean; is_admin?: boolean } }) =>
            authApi.adminUpdateUser(args.userId, args.data),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['admin', 'users'] })
        },
        onError: (err: any) => {
            const msg = err?.response?.data?.error || err?.message || 'Ошибка обновления'
            notifyError(msg)
        },
    })

    const deleteUserMutation = useMutation({
        mutationFn: (userId: string) => authApi.adminDeleteUser(userId),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['admin', 'users'] })
            notifySuccess('Пользователь удалён')
        },
        onError: (err: any) => {
            const msg = err?.response?.data?.error || err?.message || 'Ошибка удаления'
            notifyError(msg)
        },
    })

    const resetPasswordMutation = useMutation({
        mutationFn: (userId: string) => authApi.adminResetUserPassword(userId),
        onSuccess: (data) => {
            setResetResult({ userId: data.user.id, password: data.new_password })
            queryClient.invalidateQueries({ queryKey: ['admin', 'users'] })
            notifySuccess('Пароль сброшен')
        },
        onError: (err: any) => {
            const msg = err?.response?.data?.error || err?.message || 'Ошибка сброса пароля'
            notifyError(msg)
        },
    })

    // --- Invites ---
    const [showCreateInvite, setShowCreateInvite] = useState(false)
    const [inviteEmailHint, setInviteEmailHint] = useState('')
    const [inviteExpires, setInviteExpires] = useState('72')
    const [showActiveOnly, setShowActiveOnly] = useState(true)
    const [copiedToken, setCopiedToken] = useState<string | null>(null)

    const invitesQuery = useQuery({
        queryKey: ['admin', 'invites', showActiveOnly],
        queryFn: () => authApi.adminListInvites(showActiveOnly),
        retry: 1,
    })

    const createInviteMutation = useMutation({
        mutationFn: () =>
            authApi.adminCreateInvite({
                email_hint: inviteEmailHint.trim() || undefined,
                expires_in_hours: parseInt(inviteExpires) || 72,
            }),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['admin', 'invites'] })
            setShowCreateInvite(false)
            setInviteEmailHint('')
            setInviteExpires('72')
            notifySuccess('Инвайт создан')
        },
        onError: (err: any) => {
            const msg = err?.response?.data?.error || err?.message || 'Ошибка создания инвайта'
            notifyError(msg)
        },
    })

    const revokeInviteMutation = useMutation({
        mutationFn: (token: string) => authApi.adminRevokeInvite(token),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['admin', 'invites'] })
            notifySuccess('Инвайт отозван')
        },
        onError: (err: any) => {
            const msg = err?.response?.data?.error || err?.message || 'Ошибка отзыва инвайта'
            notifyError(msg)
        },
    })

    async function copyText(text: string): Promise<boolean> {
        if (navigator.clipboard) {
            try {
                await navigator.clipboard.writeText(text)
                return true
            } catch {
                // fallback below
            }
        }
        try {
            const ta = document.createElement('textarea')
            ta.value = text
            ta.style.position = 'fixed'
            ta.style.left = '-9999px'
            document.body.appendChild(ta)
            ta.select()
            document.execCommand('copy')
            document.body.removeChild(ta)
            return true
        } catch {
            return false
        }
    }

    async function handleCopyToken(token: string) {
        const ok = await copyText(token)
        if (ok) {
            setCopiedToken(token)
            setTimeout(() => setCopiedToken(null), 2000)
        } else {
            notifyError('Не удалось скопировать токен')
        }
    }

    async function handleCopyInviteLink(token: string) {
        const link = `${window.location.origin}/register?token=${token}`
        const ok = await copyText(link)
        if (ok) {
            setCopiedToken(`link:${token}`)
            setTimeout(() => setCopiedToken(null), 2000)
        } else {
            notifyError('Не удалось скопировать ссылку')
        }
    }

    // --- System Roles tab state ---
    const [rolesModalUser, setRolesModalUser] = useState<{ id: string; username: string } | null>(null)
    const [showCreateRole, setShowCreateRole] = useState(false)
    const [newRoleName, setNewRoleName] = useState('')
    const [newRoleDescription, setNewRoleDescription] = useState('')

    const systemRolesQuery = useQuery({
        queryKey: ['system-roles'],
        queryFn: () => permissionsApi.listSystemRoles(),
        enabled: tab === 'system-roles',
    })

    const createRoleMutation = useMutation({
        mutationFn: () =>
            permissionsApi.createSystemRole({
                name: newRoleName.trim(),
                description: newRoleDescription.trim() || null,
            }),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['system-roles'] })
            setShowCreateRole(false)
            setNewRoleName('')
            setNewRoleDescription('')
            notifySuccess('Роль создана')
        },
        onError: (err: any) => {
            const msg = err?.response?.data?.error || err?.message || 'Ошибка создания роли'
            notifyError(msg)
        },
    })

    const deleteRoleMutation = useMutation({
        mutationFn: (roleId: string) => permissionsApi.deleteSystemRole(roleId),
        onSuccess: () => {
            queryClient.invalidateQueries({ queryKey: ['system-roles'] })
            notifySuccess('Роль удалена')
        },
        onError: (err: any) => {
            const msg = err?.response?.data?.error || err?.message || 'Ошибка удаления роли'
            notifyError(msg)
        },
    })

    const systemRoles: Role[] = systemRolesQuery.data ?? []

    const users: AdminUser[] = usersQuery.data ?? []
    const invites: AdminInviteToken[] = invitesQuery.data ?? []

    return (
        <div className="admin-users-page">
            {rolesModalUser && (
                <UserRolesModal
                    userId={rolesModalUser.id}
                    username={rolesModalUser.username}
                    isOpen={true}
                    onClose={() => setRolesModalUser(null)}
                />
            )}

            <div className="page-tabs">
                <button
                    className={`tab-btn ${tab === 'users' ? 'active' : ''}`}
                    onClick={() => setTab('users')}
                >
                    Пользователи
                </button>
                <button
                    className={`tab-btn ${tab === 'system-roles' ? 'active' : ''}`}
                    onClick={() => setTab('system-roles')}
                >
                    Системные роли
                </button>
                <button
                    className={`tab-btn ${tab === 'invites' ? 'active' : ''}`}
                    onClick={() => setTab('invites')}
                >
                    Инвайты
                </button>
            </div>

            {tab === 'users' && (
                <div className="tab-panel card">
                    <div className="card-header">
                        <h3>Пользователи</h3>
                        <div className="header-controls">
                            <input
                                type="text"
                                className="search-input"
                                placeholder="Поиск по имени или email..."
                                value={search}
                                onChange={(e) => setSearch(e.target.value)}
                            />
                            <select
                                className="filter-select"
                                value={filterActive}
                                onChange={(e) => setFilterActive(e.target.value as typeof filterActive)}
                            >
                                <option value="all">Все</option>
                                <option value="active">Активные</option>
                                <option value="inactive">Деактивированные</option>
                            </select>
                        </div>
                    </div>

                    {usersQuery.isLoading && <Loading message="Загрузка пользователей..." />}
                    {usersQuery.isError && (
                        <ErrorComponent
                            message={
                                usersQuery.error instanceof Error
                                    ? usersQuery.error.message
                                    : 'Ошибка загрузки пользователей'
                            }
                        />
                    )}

                    {!usersQuery.isLoading && !usersQuery.isError && users.length === 0 && (
                        <EmptyState message="Пользователей не найдено" />
                    )}

                    {users.length > 0 && (
                        <div className="users-table-wrap">
                            <table className="users-table">
                                <thead>
                                    <tr>
                                        <th>Пользователь</th>
                                        <th>Email</th>
                                        <th>Статус</th>
                                        <th>Роль</th>
                                        <th>Дата регистрации</th>
                                        <th>Действия</th>
                                    </tr>
                                </thead>
                                <tbody>
                                    {users.map((u) => (
                                        <tr key={u.id} className={u.is_active ? '' : 'row-inactive'}>
                                            <td className="user-cell">
                                                <span className="username">{u.username}</span>
                                                {u.password_change_required && (
                                                    <span className="badge badge-warn" title="Требуется смена пароля">
                                                        pwd
                                                    </span>
                                                )}
                                            </td>
                                            <td>{u.email}</td>
                                            <td>
                                                <span
                                                    className={`badge ${u.is_active ? 'badge-success' : 'badge-danger'}`}
                                                >
                                                    {u.is_active ? 'Активен' : 'Деактивирован'}
                                                </span>
                                            </td>
                                            <td>
                                                {(u.system_roles ?? []).length > 0 ? (
                                                    (u.system_roles ?? []).map((r) => (
                                                        <span key={r} className="badge badge-admin">{r}</span>
                                                    ))
                                                ) : (
                                                    <span className="badge badge-user">user</span>
                                                )}
                                            </td>
                                            <td className="date-cell">
                                                {format(new Date(u.created_at), 'dd.MM.yyyy HH:mm')}
                                            </td>
                                            <td className="actions-cell">
                                                <button
                                                    className="btn btn-secondary btn-xs"
                                                    title={u.is_active ? 'Деактивировать' : 'Активировать'}
                                                    onClick={() =>
                                                        updateUserMutation.mutate({
                                                            userId: u.id,
                                                            data: { is_active: !u.is_active },
                                                        })
                                                    }
                                                    disabled={updateUserMutation.isPending}
                                                >
                                                    {u.is_active ? 'Деактив.' : 'Активир.'}
                                                </button>
                                                <button
                                                    className="btn btn-secondary btn-xs"
                                                    title={u.is_admin ? 'Убрать права admin' : 'Сделать admin'}
                                                    onClick={() =>
                                                        updateUserMutation.mutate({
                                                            userId: u.id,
                                                            data: { is_admin: !u.is_admin },
                                                        })
                                                    }
                                                    disabled={updateUserMutation.isPending}
                                                >
                                                    {u.is_admin ? '−admin' : '+admin'}
                                                </button>
                                                <button
                                                    className="btn btn-secondary btn-xs"
                                                    title="Управление ролями"
                                                    onClick={() =>
                                                        setRolesModalUser({ id: u.id, username: u.username })
                                                    }
                                                >
                                                    Роли
                                                </button>
                                                <button
                                                    className="btn btn-secondary btn-xs"
                                                    title="Сбросить пароль"
                                                    onClick={() => resetPasswordMutation.mutate(u.id)}
                                                    disabled={resetPasswordMutation.isPending}
                                                >
                                                    Пароль↺
                                                </button>
                                                {resetResult?.userId === u.id && (
                                                    <span
                                                        className="reset-password-result"
                                                        title="Нажмите чтобы скопировать"
                                                        onClick={() => {
                                                            void copyText(resetResult.password).then((ok) => {
                                                                if (ok) notifySuccess('Пароль скопирован')
                                                                else notifyError('Не удалось скопировать пароль')
                                                            })
                                                        }}
                                                    >
                                                        🔑 {resetResult.password}
                                                    </span>
                                                )}
                                                <button
                                                    className="btn btn-danger btn-xs"
                                                    title="Удалить пользователя"
                                                    onClick={() => {
                                                        if (confirm(`Удалить пользователя ${u.username}?`)) {
                                                            deleteUserMutation.mutate(u.id)
                                                        }
                                                    }}
                                                    disabled={deleteUserMutation.isPending}
                                                >
                                                    Удалить
                                                </button>
                                            </td>
                                        </tr>
                                    ))}
                                </tbody>
                            </table>
                        </div>
                    )}
                </div>
            )}

            {tab === 'system-roles' && (
                <div className="tab-panel card">
                    <div className="card-header">
                        <h3>Системные роли</h3>
                        <div className="header-controls">
                            <PermissionGate permission="roles.manage" system>
                                <button
                                    className="btn btn-primary btn-sm"
                                    onClick={() => setShowCreateRole((v) => !v)}
                                >
                                    {showCreateRole ? 'Отмена' : 'Создать роль'}
                                </button>
                            </PermissionGate>
                        </div>
                    </div>

                    {showCreateRole && (
                        <Box sx={{ p: 2, borderBottom: '1px solid', borderColor: 'divider', bgcolor: 'grey.50' }}>
                            <Typography variant="subtitle2" gutterBottom>
                                Новая системная роль
                            </Typography>
                            <Box display="flex" flexDirection="column" gap={1.5} maxWidth={480}>
                                <TextField
                                    label="Название"
                                    size="small"
                                    value={newRoleName}
                                    onChange={(e) => setNewRoleName(e.target.value)}
                                    required
                                />
                                <TextField
                                    label="Описание (необязательно)"
                                    size="small"
                                    value={newRoleDescription}
                                    onChange={(e) => setNewRoleDescription(e.target.value)}
                                    multiline
                                    minRows={2}
                                />
                                <Box display="flex" gap={1} justifyContent="flex-end">
                                    <Button
                                        size="small"
                                        variant="outlined"
                                        onClick={() => setShowCreateRole(false)}
                                    >
                                        Отмена
                                    </Button>
                                    <Button
                                        size="small"
                                        variant="contained"
                                        disabled={!newRoleName.trim() || createRoleMutation.isPending}
                                        onClick={() => createRoleMutation.mutate()}
                                    >
                                        {createRoleMutation.isPending ? 'Создание...' : 'Создать'}
                                    </Button>
                                </Box>
                            </Box>
                        </Box>
                    )}

                    {systemRolesQuery.isLoading && <Loading message="Загрузка ролей..." />}
                    {systemRolesQuery.isError && (
                        <ErrorComponent
                            message={
                                systemRolesQuery.error instanceof Error
                                    ? systemRolesQuery.error.message
                                    : 'Ошибка загрузки ролей'
                            }
                        />
                    )}

                    {!systemRolesQuery.isLoading && !systemRolesQuery.isError && systemRoles.length === 0 && (
                        <EmptyState message="Системных ролей нет" />
                    )}

                    {systemRoles.length > 0 && (
                        <List dense>
                            {systemRoles.map((role) => (
                                <ListItem
                                    key={role.id}
                                    divider
                                    secondaryAction={
                                        role.is_builtin ? (
                                            <Chip label="встроенная" size="small" variant="outlined" />
                                        ) : (
                                            <PermissionGate permission="roles.manage" system>
                                                <Button
                                                    size="small"
                                                    color="error"
                                                    variant="outlined"
                                                    disabled={deleteRoleMutation.isPending}
                                                    onClick={() => {
                                                        if (confirm(`Удалить роль «${role.name}»?`)) {
                                                            deleteRoleMutation.mutate(role.id)
                                                        }
                                                    }}
                                                >
                                                    Удалить
                                                </Button>
                                            </PermissionGate>
                                        )
                                    }
                                >
                                    <ListItemText
                                        primary={
                                            <Box display="flex" alignItems="center" gap={1}>
                                                <Typography variant="body2" fontWeight={600}>
                                                    {role.name}
                                                </Typography>
                                                {role.is_builtin && (
                                                    <Chip label="builtin" size="small" color="default" />
                                                )}
                                            </Box>
                                        }
                                        secondary={role.description ?? undefined}
                                    />
                                </ListItem>
                            ))}
                        </List>
                    )}
                </div>
            )}

            {tab === 'invites' && (
                <div className="tab-panel card">
                    <div className="card-header">
                        <h3>Инвайты</h3>
                        <div className="header-controls">
                            <label className="toggle-label">
                                <input
                                    type="checkbox"
                                    checked={showActiveOnly}
                                    onChange={(e) => setShowActiveOnly(e.target.checked)}
                                />
                                Только активные
                            </label>
                            <button
                                className="btn btn-primary btn-sm"
                                onClick={() => setShowCreateInvite((v) => !v)}
                            >
                                {showCreateInvite ? 'Отмена' : 'Создать инвайт'}
                            </button>
                        </div>
                    </div>

                    {showCreateInvite && (
                        <div className="invite-create-form">
                            <div className="form-row">
                                <div className="form-group">
                                    <label htmlFor="invite-email">Email (подсказка, необязательно)</label>
                                    <input
                                        id="invite-email"
                                        type="email"
                                        placeholder="user@example.com"
                                        value={inviteEmailHint}
                                        onChange={(e) => setInviteEmailHint(e.target.value)}
                                    />
                                </div>
                                <div className="form-group form-group--sm">
                                    <label htmlFor="invite-expires">Срок действия (часов)</label>
                                    <input
                                        id="invite-expires"
                                        type="number"
                                        min="1"
                                        max="8760"
                                        value={inviteExpires}
                                        onChange={(e) => setInviteExpires(e.target.value)}
                                    />
                                </div>
                            </div>
                            <div className="form-actions">
                                <button
                                    className="btn btn-secondary btn-sm"
                                    onClick={() => setShowCreateInvite(false)}
                                >
                                    Отмена
                                </button>
                                <button
                                    className="btn btn-primary btn-sm"
                                    onClick={() => createInviteMutation.mutate()}
                                    disabled={createInviteMutation.isPending}
                                >
                                    {createInviteMutation.isPending ? 'Создание...' : 'Создать'}
                                </button>
                            </div>
                        </div>
                    )}

                    {invitesQuery.isLoading && <Loading message="Загрузка инвайтов..." />}
                    {invitesQuery.isError && (
                        <ErrorComponent
                            message={
                                invitesQuery.error instanceof Error
                                    ? invitesQuery.error.message
                                    : 'Ошибка загрузки инвайтов'
                            }
                        />
                    )}

                    {!invitesQuery.isLoading && !invitesQuery.isError && invites.length === 0 && (
                        <EmptyState message="Инвайтов нет">
                            {!showCreateInvite && (
                                <button
                                    className="btn btn-primary btn-sm"
                                    onClick={() => setShowCreateInvite(true)}
                                >
                                    Создать первый инвайт
                                </button>
                            )}
                        </EmptyState>
                    )}

                    {invites.length > 0 && (
                        <div className="invites-table-wrap">
                            <table className="invites-table">
                                <thead>
                                    <tr>
                                        <th>Токен</th>
                                        <th>Email</th>
                                        <th>Статус</th>
                                        <th>Истекает</th>
                                        <th>Использован</th>
                                        <th>Действия</th>
                                    </tr>
                                </thead>
                                <tbody>
                                    {invites.map((inv) => (
                                        <tr key={inv.id} className={inv.is_active ? '' : 'row-inactive'}>
                                            <td className="token-cell">
                                                <code className="token-value" title={inv.token}>
                                                    {inv.token.slice(0, 8)}…
                                                </code>
                                                <button
                                                    className="btn btn-ghost btn-xs"
                                                    title="Копировать токен"
                                                    onClick={() => handleCopyToken(inv.token)}
                                                >
                                                    {copiedToken === inv.token ? '✓' : '⎘'}
                                                </button>
                                                <button
                                                    className="btn btn-ghost btn-xs"
                                                    title="Скопировать ссылку для регистрации"
                                                    onClick={() => handleCopyInviteLink(inv.token)}
                                                >
                                                    {copiedToken === `link:${inv.token}` ? '✓' : '🔗'}
                                                </button>
                                            </td>
                                            <td>{inv.email_hint ?? '—'}</td>
                                            <td>
                                                <span
                                                    className={`badge ${inv.is_active ? 'badge-success' : 'badge-muted'}`}
                                                >
                                                    {inv.is_active ? 'Активен' : 'Использован/истёк'}
                                                </span>
                                            </td>
                                            <td className="date-cell">
                                                {format(new Date(inv.expires_at), 'dd.MM.yyyy HH:mm')}
                                            </td>
                                            <td className="date-cell">
                                                {inv.used_at
                                                    ? format(new Date(inv.used_at), 'dd.MM.yyyy HH:mm')
                                                    : '—'}
                                            </td>
                                            <td>
                                                {inv.is_active && !inv.used_at && (
                                                    <button
                                                        className="btn btn-danger btn-xs"
                                                        onClick={() => {
                                                            if (confirm('Отозвать инвайт?')) {
                                                                revokeInviteMutation.mutate(inv.token)
                                                            }
                                                        }}
                                                        disabled={revokeInviteMutation.isPending}
                                                    >
                                                        Отозвать
                                                    </button>
                                                )}
                                            </td>
                                        </tr>
                                    ))}
                                </tbody>
                            </table>
                        </div>
                    )}
                </div>
            )}
        </div>
    )
}

export default AdminUsers
