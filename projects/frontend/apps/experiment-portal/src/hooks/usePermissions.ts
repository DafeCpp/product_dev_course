import { useQuery } from '@tanstack/react-query'
import { authApi } from '../api/auth'
import { permissionsApi } from '../api/permissions'
import { getActiveProjectId } from '../utils/activeProject'

export function usePermissions() {
  const { data: user } = useQuery({
    queryKey: ['auth', 'me'],
    queryFn: () => authApi.me(),
    staleTime: 5 * 60 * 1000,
  })

  const projectId = getActiveProjectId() ?? undefined

  const { data: permissions, isLoading } = useQuery({
    queryKey: ['permissions', 'effective', user?.id, projectId],
    queryFn: () => permissionsApi.getEffectivePermissions(user!.id, projectId),
    enabled: !!user?.id,
    staleTime: 30 * 1000,
  })

  const systemPermissions = permissions?.system_permissions ?? []
  const projectPermissions = permissions?.project_permissions ?? []

  const hasPermission = (perm: string): boolean => {
    if (!permissions) return false
    if (permissions.is_superadmin) return true
    return (
      permissions.system_permissions.includes(perm) ||
      permissions.project_permissions.includes(perm)
    )
  }

  const hasSystemPermission = (perm: string): boolean => {
    if (!permissions) return false
    if (permissions.is_superadmin) return true
    return permissions.system_permissions.includes(perm)
  }

  const hasAnyPermission = (...perms: string[]): boolean => {
    return perms.some(hasPermission)
  }

  return {
    systemPermissions,
    projectPermissions,
    permissions: [...systemPermissions, ...projectPermissions],
    isSuperadmin: permissions?.is_superadmin ?? false,
    hasPermission,
    hasSystemPermission,
    hasAnyPermission,
    isLoading,
  }
}
