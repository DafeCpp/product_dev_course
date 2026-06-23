import { useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { useApiMutation } from '../../hooks/useApiMutation'
import { permissionsApi } from '../../api/permissions'
import type { Role } from '../../types/permissions'
import PermissionGate from '../PermissionGate'
import { Loading } from '../common'
import CreateProjectRoleModal from './CreateProjectRoleModal'

interface ProjectRolesSectionProps {
    projectId: string
}

function ProjectRolesSection({ projectId }: ProjectRolesSectionProps) {
    const [isCreateModalOpen, setIsCreateModalOpen] = useState(false)
    const [isExpanded, setIsExpanded] = useState(false)

    const { data: projectRoles = [], isLoading } = useQuery({
        queryKey: ['projects', projectId, 'roles'],
        queryFn: () => permissionsApi.listProjectRoles(projectId),
        staleTime: 30_000,
    })

    const deleteMutation = useApiMutation<unknown, string>({
        mutationFn: (roleId) => permissionsApi.deleteProjectRole(projectId, roleId),
        invalidateKeys: [['projects', projectId, 'roles']],
        successMessage: 'Роль удалена',
        errorFallback: 'Ошибка удаления роли',
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

export default ProjectRolesSection
