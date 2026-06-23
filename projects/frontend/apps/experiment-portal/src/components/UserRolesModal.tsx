import {
    Dialog,
    DialogTitle,
    DialogContent,
    DialogActions,
    Button,
    Typography,
    Box,
    Chip,
    List,
    ListItem,
    ListItemText,
    ListItemSecondaryAction,
    CircularProgress,
    Divider,
    Alert,
} from '@mui/material'
import { useQuery } from '@tanstack/react-query'
import { permissionsApi } from '../api/permissions'
import { usePermissions } from '../hooks/usePermissions'
import { useApiMutation } from '../hooks/useApiMutation'
import { notifySuccess } from '../utils/notify'
import type { Role } from '../types/permissions'

interface Props {
    userId: string
    username: string
    isOpen: boolean
    onClose: () => void
}

function UserRolesModal({ userId, username, isOpen, onClose }: Props) {
    const { hasSystemPermission } = usePermissions()
    const canAssign = hasSystemPermission('roles.assign')

    const effectivePermsQuery = useQuery({
        queryKey: ['permissions', 'effective', userId],
        queryFn: () => permissionsApi.getEffectivePermissions(userId),
        enabled: isOpen,
    })

    const allRolesQuery = useQuery({
        queryKey: ['system-roles'],
        queryFn: () => permissionsApi.listSystemRoles(),
        enabled: isOpen,
    })

    const grantMutation = useApiMutation({
        mutationFn: (role: Role) =>
            permissionsApi.grantSystemRole(userId, { role_id: role.id }),
        invalidateKeys: [['permissions', 'effective', userId]],
        errorFallback: 'Ошибка назначения роли',
        onSuccess: (_data, role) => notifySuccess(`Роль «${role.name}» назначена`),
    })

    const revokeMutation = useApiMutation({
        mutationFn: (role: Role) =>
            permissionsApi.revokeSystemRole(userId, role.id),
        invalidateKeys: [['permissions', 'effective', userId]],
        errorFallback: 'Ошибка отзыва роли',
        onSuccess: (_data, role) => notifySuccess(`Роль «${role.name}» отозвана`),
    })

    const userSystemRoles: string[] = effectivePermsQuery.data?.system_permissions ?? []
    const allRoles: Role[] = allRolesQuery.data ?? []

    const isLoading = effectivePermsQuery.isLoading || allRolesQuery.isLoading
    const isMutating = grantMutation.isPending || revokeMutation.isPending

    // Determine which roles the user currently has by matching role names against system_permissions.
    // The backend returns role names in effective system_permissions.
    const userRoleNames = new Set(userSystemRoles)

    return (
        <Dialog open={isOpen} onClose={onClose} maxWidth="sm" fullWidth>
            <DialogTitle>
                Системные роли: {username}
            </DialogTitle>

            <DialogContent dividers>
                {isLoading && (
                    <Box display="flex" justifyContent="center" py={3}>
                        <CircularProgress size={32} />
                    </Box>
                )}

                {!isLoading && effectivePermsQuery.isError && (
                    <Alert severity="error">Ошибка загрузки данных пользователя</Alert>
                )}

                {!isLoading && !effectivePermsQuery.isError && (
                    <>
                        <Typography variant="subtitle2" gutterBottom>
                            Текущие системные права
                        </Typography>
                        {userSystemRoles.length === 0 ? (
                            <Typography variant="body2" color="text.secondary" mb={2}>
                                Нет системных прав
                            </Typography>
                        ) : (
                            <Box display="flex" flexWrap="wrap" gap={0.5} mb={2}>
                                {userSystemRoles.map((p) => (
                                    <Chip key={p} label={p} size="small" color="secondary" variant="outlined" />
                                ))}
                            </Box>
                        )}

                        <Divider sx={{ my: 1.5 }} />

                        <Typography variant="subtitle2" gutterBottom>
                            Доступные системные роли
                        </Typography>

                        {allRoles.length === 0 && (
                            <Typography variant="body2" color="text.secondary">
                                Системных ролей не найдено
                            </Typography>
                        )}

                        <List dense disablePadding>
                            {allRoles.map((role) => {
                                const assigned = userRoleNames.has(role.name)
                                return (
                                    <ListItem key={role.id} disablePadding sx={{ py: 0.5 }}>
                                        <ListItemText
                                            primary={role.name}
                                            secondary={role.description ?? undefined}
                                            primaryTypographyProps={{ variant: 'body2', fontWeight: 500 }}
                                            secondaryTypographyProps={{ variant: 'caption' }}
                                        />
                                        <ListItemSecondaryAction>
                                            {assigned ? (
                                                <Button
                                                    size="small"
                                                    color="error"
                                                    variant="outlined"
                                                    disabled={!canAssign || isMutating}
                                                    onClick={() => revokeMutation.mutate(role)}
                                                >
                                                    Отозвать
                                                </Button>
                                            ) : (
                                                <Button
                                                    size="small"
                                                    color="primary"
                                                    variant="outlined"
                                                    disabled={!canAssign || isMutating}
                                                    onClick={() => grantMutation.mutate(role)}
                                                >
                                                    Назначить
                                                </Button>
                                            )}
                                        </ListItemSecondaryAction>
                                    </ListItem>
                                )
                            })}
                        </List>

                        {!canAssign && (
                            <Alert severity="info" sx={{ mt: 2 }}>
                                У вас нет прав для управления ролями
                            </Alert>
                        )}
                    </>
                )}
            </DialogContent>

            <DialogActions>
                <Button onClick={onClose}>Закрыть</Button>
            </DialogActions>
        </Dialog>
    )
}

export default UserRolesModal
