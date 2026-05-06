import Chip from '@mui/material/Chip'
import type { ProjectMember } from '../../types'
import type { Role } from '../../types/permissions'
import { MaterialSelect } from '../common'

interface MembersTableProps {
    members: ProjectMember[]
    projectRoles: Role[]
    currentUserId: string | null
    projectOwnerId: string
    isOwner: boolean
    isPending: boolean
    onRoleChange: (userId: string, newRole: 'owner' | 'editor' | 'viewer') => void
    onManageRoles: (member: { userId: string; username: string; assignedRoleIds: string[] }) => void
    onRemoveMember: (userId: string, username?: string | null) => void
}

function getRoleLabel(role: string): string {
    const labels: Record<string, string> = {
        owner: 'Владелец',
        editor: 'Редактор',
        viewer: 'Наблюдатель',
    }
    return labels[role] || role
}

function MembersTable({
    members,
    projectRoles,
    currentUserId,
    projectOwnerId,
    isOwner,
    isPending,
    onRoleChange,
    onManageRoles,
    onRemoveMember,
}: MembersTableProps) {
    if (members.length === 0) {
        return <p className="empty-message">Нет участников</p>
    }

    return (
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
                    {members.map((member) => {
                        const isCurrentUser = member.user_id === currentUserId
                        const isMemberOwner = member.user_id === projectOwnerId
                        const canEdit = isOwner && !isMemberOwner

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
                                                    onRoleChange(
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
                                                            onManageRoles({
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
                                                            onRemoveMember(member.user_id, member.username)
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
    )
}

export default MembersTable
