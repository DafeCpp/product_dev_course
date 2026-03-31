import type { ReactNode } from 'react'
import { usePermissions } from '../hooks/usePermissions'

interface Props {
  permission: string
  system?: boolean
  fallback?: ReactNode
  children: ReactNode
}

function PermissionGate({ permission, system = false, fallback = null, children }: Props) {
  const { hasPermission, hasSystemPermission } = usePermissions()

  const allowed = system ? hasSystemPermission(permission) : hasPermission(permission)

  return <>{allowed ? children : fallback}</>
}

export default PermissionGate
