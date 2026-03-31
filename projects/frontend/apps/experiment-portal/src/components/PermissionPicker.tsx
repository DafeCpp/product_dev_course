import { useMemo } from 'react'
import { useQuery } from '@tanstack/react-query'
import { permissionsApi } from '../api/permissions'
import type { Permission } from '../types/permissions'
import './PermissionPicker.scss'

interface Props {
  scope: 'system' | 'project'
  selected: string[]
  onChange: (selected: string[]) => void
  disabled?: boolean
}

function PermissionPicker({ scope, selected, onChange, disabled = false }: Props) {
  const { data: allPermissions = [], isLoading } = useQuery({
    queryKey: ['permissions', 'list'],
    queryFn: () => permissionsApi.listPermissions(),
    staleTime: 60_000,
  })

  const filtered = useMemo(
    () => allPermissions.filter((p) => p.scope === scope),
    [allPermissions, scope]
  )

  const grouped = useMemo(() => {
    const map = new Map<string, Permission[]>()
    for (const perm of filtered) {
      const group = map.get(perm.category) ?? []
      group.push(perm)
      map.set(perm.category, group)
    }
    return map
  }, [filtered])

  const selectedSet = useMemo(() => new Set(selected), [selected])

  function togglePermission(name: string) {
    if (disabled) return
    const next = new Set(selectedSet)
    if (next.has(name)) {
      next.delete(name)
    } else {
      next.add(name)
    }
    onChange(Array.from(next))
  }

  function toggleGroup(_category: string, perms: Permission[]) {
    if (disabled) return
    const names = perms.map((p) => p.name)
    const allSelected = names.every((n) => selectedSet.has(n))
    const next = new Set(selectedSet)
    if (allSelected) {
      names.forEach((n) => next.delete(n))
    } else {
      names.forEach((n) => next.add(n))
    }
    onChange(Array.from(next))
  }

  if (isLoading) {
    return <div className="permission-picker__loading">Загрузка permissions...</div>
  }

  if (grouped.size === 0) {
    return <div className="permission-picker__empty">Нет доступных permissions</div>
  }

  return (
    <div className="permission-picker">
      {Array.from(grouped.entries()).map(([category, perms]) => {
        const allSelected = perms.every((p) => selectedSet.has(p.name))
        const someSelected = perms.some((p) => selectedSet.has(p.name))

        return (
          <div key={category} className="permission-picker__group">
            <div className="permission-picker__group-header">
              <label className="permission-picker__group-label">
                <input
                  type="checkbox"
                  checked={allSelected}
                  ref={(el) => {
                    if (el) el.indeterminate = someSelected && !allSelected
                  }}
                  onChange={() => toggleGroup(category, perms)}
                  disabled={disabled}
                />
                <span className="permission-picker__group-name">{category}</span>
              </label>
            </div>
            <div className="permission-picker__items">
              {perms.map((perm) => (
                <label key={perm.id} className="permission-picker__item">
                  <input
                    type="checkbox"
                    checked={selectedSet.has(perm.name)}
                    onChange={() => togglePermission(perm.name)}
                    disabled={disabled}
                  />
                  <span className="permission-picker__item-info">
                    <span className="permission-picker__item-name">{perm.name}</span>
                    {perm.description && (
                      <span className="permission-picker__item-desc">{perm.description}</span>
                    )}
                  </span>
                </label>
              ))}
            </div>
          </div>
        )
      })}
    </div>
  )
}

export default PermissionPicker
