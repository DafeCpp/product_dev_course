import CircularProgress from '@mui/material/CircularProgress'
import { useQuery } from '@tanstack/react-query'
import { permissionsApi } from '../../api/permissions'

interface PermissionPickerLocalProps {
    scope: 'project' | 'system'
    value: string[]
    onChange: (ids: string[]) => void
    disabled?: boolean
}

function PermissionPickerLocal({ scope, value, onChange, disabled }: PermissionPickerLocalProps) {
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

export default PermissionPickerLocal
