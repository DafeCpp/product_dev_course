import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import React from 'react'
import UserRolesModal from './UserRolesModal'
import { permissionsApi } from '../api/permissions'
import type { UserSystemRole } from '../types/permissions'

vi.mock('../api/permissions', () => ({
    permissionsApi: {
        getEffectivePermissions: vi.fn(),
        listSystemRoles: vi.fn(),
        grantSystemRole: vi.fn(),
        revokeSystemRole: vi.fn(),
    },
}))

const mockHasSystemPermission = vi.fn()

vi.mock('../hooks/usePermissions', () => ({
    usePermissions: () => ({
        hasSystemPermission: mockHasSystemPermission,
        hasPermission: vi.fn().mockReturnValue(false),
        permissions: [],
        systemPermissions: [],
        isSuperadmin: false,
        isLoading: false,
    }),
}))

vi.mock('../utils/notify', () => ({
    notifySuccess: vi.fn(),
    notifyError: vi.fn(),
}))

const createWrapper = () => {
    const queryClient = new QueryClient({
        defaultOptions: {
            queries: { retry: false },
            mutations: { retry: false },
        },
    })
    return ({ children }: { children: React.ReactNode }) =>
        React.createElement(QueryClientProvider, { client: queryClient }, children)
}

const mockSystemRoles = [
    {
        id: 'role-admin',
        name: 'admin',
        description: 'Администратор системы',
        scope: 'system' as const,
        is_builtin: true,
        project_id: null,
        permissions: [],
        created_at: '2024-01-01T00:00:00Z',
        updated_at: '2024-01-01T00:00:00Z',
    },
    {
        id: 'role-auditor',
        name: 'auditor',
        description: 'Аудитор',
        scope: 'system' as const,
        is_builtin: true,
        project_id: null,
        permissions: [],
        created_at: '2024-01-01T00:00:00Z',
        updated_at: '2024-01-01T00:00:00Z',
    },
]

const defaultProps = {
    userId: 'user-42',
    username: 'testuser',
    isOpen: true,
    onClose: vi.fn(),
}

describe('UserRolesModal', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockHasSystemPermission.mockReturnValue(true)

        vi.mocked(permissionsApi).getEffectivePermissions.mockResolvedValue({
            system_permissions: [],
            project_permissions: [],
            is_superadmin: false,
        })
        vi.mocked(permissionsApi).listSystemRoles.mockResolvedValue(mockSystemRoles)
        vi.mocked(permissionsApi).grantSystemRole.mockResolvedValue({} as UserSystemRole)
        vi.mocked(permissionsApi).revokeSystemRole.mockResolvedValue(undefined)
    })

    it('renders_system_roles_list after loading', async () => {
        render(<UserRolesModal {...defaultProps} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('admin')).toBeInTheDocument()
            expect(screen.getByText('auditor')).toBeInTheDocument()
        })
    })

    it('renders_role_descriptions alongside role names', async () => {
        render(<UserRolesModal {...defaultProps} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Администратор системы')).toBeInTheDocument()
            expect(screen.getByText('Аудитор')).toBeInTheDocument()
        })
    })

    it('grant_button_calls_grantSystemRole when user does not have the role', async () => {
        const user = userEvent.setup()
        vi.mocked(permissionsApi).getEffectivePermissions.mockResolvedValue({
            system_permissions: [],
            project_permissions: [],
            is_superadmin: false,
        })

        render(<UserRolesModal {...defaultProps} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getAllByRole('button', { name: /назначить/i }).length).toBeGreaterThan(0)
        })

        const grantButtons = screen.getAllByRole('button', { name: /назначить/i })
        await user.click(grantButtons[0])

        await waitFor(() => {
            expect(vi.mocked(permissionsApi).grantSystemRole).toHaveBeenCalledWith(
                'user-42',
                { role_id: mockSystemRoles[0].id }
            )
        })
    })

    it('revoke_button_calls_revokeSystemRole when user already has the role', async () => {
        const user = userEvent.setup()
        vi.mocked(permissionsApi).getEffectivePermissions.mockResolvedValue({
            system_permissions: ['admin'],
            project_permissions: [],
            is_superadmin: false,
        })

        render(<UserRolesModal {...defaultProps} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByRole('button', { name: /отозвать/i })).toBeInTheDocument()
        })

        await user.click(screen.getByRole('button', { name: /отозвать/i }))

        await waitFor(() => {
            expect(vi.mocked(permissionsApi).revokeSystemRole).toHaveBeenCalledWith(
                'user-42',
                mockSystemRoles[0].id
            )
        })
    })

    it('action_buttons_are_disabled when roles.assign permission is absent', async () => {
        mockHasSystemPermission.mockReturnValue(false)

        render(<UserRolesModal {...defaultProps} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('admin')).toBeInTheDocument()
        })

        const buttons = screen.getAllByRole('button').filter(
            (btn) =>
                btn.textContent?.toLowerCase().includes('назначить') ||
                btn.textContent?.toLowerCase().includes('отозвать')
        )
        buttons.forEach((btn) => {
            expect(btn).toBeDisabled()
        })
    })

    it('shows_info_alert when canAssign is false', async () => {
        mockHasSystemPermission.mockReturnValue(false)

        render(<UserRolesModal {...defaultProps} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/нет прав для управления ролями/i)).toBeInTheDocument()
        })
    })

    it('does_not_render_content when isOpen is false', () => {
        render(<UserRolesModal {...defaultProps} isOpen={false} />, {
            wrapper: createWrapper(),
        })

        expect(screen.queryByText('admin')).not.toBeInTheDocument()
    })

    it('shows_username_in_dialog_title', async () => {
        render(<UserRolesModal {...defaultProps} username="alice" />, {
            wrapper: createWrapper(),
        })

        await waitFor(() => {
            expect(screen.getByText(/alice/i)).toBeInTheDocument()
        })
    })

    it('shows_loading_spinner_while_data_is_fetching', () => {
        vi.mocked(permissionsApi).listSystemRoles.mockImplementation(
            () => new Promise(() => {})
        )

        render(<UserRolesModal {...defaultProps} />, { wrapper: createWrapper() })

        expect(screen.getByRole('progressbar')).toBeInTheDocument()
    })

    it('shows_error_alert_when_effective_permissions_query_fails', async () => {
        vi.mocked(permissionsApi).getEffectivePermissions.mockRejectedValue(
            new Error('Server error')
        )

        render(<UserRolesModal {...defaultProps} />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/ошибка загрузки данных пользователя/i)).toBeInTheDocument()
        })
    })
})
