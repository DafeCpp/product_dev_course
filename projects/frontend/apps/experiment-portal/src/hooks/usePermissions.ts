import { useQuery } from '@tanstack/react-query'
import { authApi } from '../api/auth'
import { permissionsApi } from '../api/permissions'
import { getActiveProjectId } from '../utils/activeProject'

export interface UsePermissionsResult {
  permissions: string[]
  systemPermissions: string[]
  isSuperadmin: boolean
  hasPermission: (permission: string) => boolean
  hasSystemPermission: (permission: string) => boolean
  isLoading: boolean
}

export function usePermissions(): UsePermissionsResult {
  const { data: currentUser } = useQuery({
    queryKey: ['auth', 'me'],
    queryFn: () => authApi.me(),
    retry: false,
    staleTime: 30_000,
  })

  const activeProjectId = getActiveProjectId() ?? undefined

  const { data: effectivePerms, isLoading } = useQuery({
    queryKey: ['permissions', 'effective', currentUser?.id, activeProjectId],
    queryFn: () => permissionsApi.getEffectivePermissions(currentUser!.id, activeProjectId),
    enabled: Boolean(currentUser?.id),
    staleTime: 30_000,
  })

  const permissions = effectivePerms?.project_permissions ?? []
  const systemPermissions = effectivePerms?.system_permissions ?? []
  const isSuperadmin = effectivePerms?.is_superadmin ?? false

  const hasPermission = (permission: string): boolean => {
    if (isSuperadmin) return true
    return permissions.includes(permission) || systemPermissions.includes(permission)
  }

  const hasSystemPermission = (permission: string): boolean => {
    if (isSuperadmin) return true
    return systemPermissions.includes(permission)
  }

  return {
    permissions,
    systemPermissions,
    isSuperadmin,
    hasPermission,
    hasSystemPermission,
    isLoading: isLoading && Boolean(currentUser?.id),
  }
}
