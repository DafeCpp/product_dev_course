import { ReactNode } from 'react'
import { usePermissions } from '../hooks/usePermissions'

interface PermissionGateProps {
  permission?: string
  anyOf?: string[]
  system?: boolean
  fallback?: ReactNode
  children: ReactNode
}

export function PermissionGate({
  permission,
  anyOf,
  system,
  fallback = null,
  children,
}: PermissionGateProps) {
  const { hasPermission, hasSystemPermission, hasAnyPermission } = usePermissions()

  let allowed = false
  if (permission) {
    allowed = system ? hasSystemPermission(permission) : hasPermission(permission)
  } else if (anyOf && anyOf.length > 0) {
    allowed = hasAnyPermission(...anyOf)
  } else {
    allowed = true
  }

  return allowed ? <>{children}</> : <>{fallback}</>
}

export default PermissionGate
