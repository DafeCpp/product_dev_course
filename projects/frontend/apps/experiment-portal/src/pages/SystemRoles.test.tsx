import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import { fireEvent } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { MemoryRouter } from 'react-router-dom'
import SystemRoles from './SystemRoles'

// ---------------------------------------------------------------------------
// API mock
// ---------------------------------------------------------------------------
vi.mock('../api/permissions', () => ({
    permissionsApi: {
        listSystemRoles: vi.fn(),
        createSystemRole: vi.fn(),
        updateSystemRole: vi.fn(),
        deleteSystemRole: vi.fn(),
        listPermissions: vi.fn(),
        getEffectivePermissions: vi.fn(),
    },
}))

// ---------------------------------------------------------------------------
// Permissions hook mock — default: has roles.manage
// ---------------------------------------------------------------------------
const mockHasSystemPermission = vi.fn((_perm: string) => true)

vi.mock('../hooks/usePermissions', () => ({
    usePermissions: () => ({
        hasSystemPermission: mockHasSystemPermission,
        hasPermission: vi.fn(() => true),
        hasAnyPermission: vi.fn(() => true),
        isSuperadmin: false,
        systemPermissions: [],
        projectPermissions: [],
        permissions: [],
        isLoading: false,
    }),
}))

// ---------------------------------------------------------------------------
// PermissionGate — passthrough stub
// ---------------------------------------------------------------------------
vi.mock('../components/PermissionGate', () => ({
    default: ({ children }: { children: React.ReactNode }) => <>{children}</>,
    PermissionGate: ({ children }: { children: React.ReactNode }) => <>{children}</>,
}))

// ---------------------------------------------------------------------------
// PermissionPicker — minimal stub (no extra API calls)
// ---------------------------------------------------------------------------
vi.mock('../components/PermissionPicker', () => ({
    default: ({
        onChange,
    }: {
        scope: string
        selected: string[]
        onChange: (s: string[]) => void
        disabled?: boolean
    }) => (
        <div data-testid="permission-picker-stub">
            <button
                type="button"
                onClick={() => onChange(['roles.manage'])}
            >
                Select permission
            </button>
        </div>
    ),
}))

vi.mock('../utils/notify', () => ({
    notifyError: vi.fn(),
    notifySuccess: vi.fn(),
}))

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function createWrapper() {
    const queryClient = new QueryClient({
        defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
    })
    return ({ children }: { children: React.ReactNode }) => (
        <QueryClientProvider client={queryClient}>
            <MemoryRouter>{children}</MemoryRouter>
        </QueryClientProvider>
    )
}

import { permissionsApi } from '../api/permissions'
import { notifyError } from '../utils/notify'

const mockRoleCustom = {
    id: 'role-custom-1',
    name: 'Custom Role',
    description: 'A custom role',
    scope: 'system' as const,
    is_builtin: false,
    project_id: null,
    permissions: [
        { id: 'perm-1', name: 'roles.manage', description: '', category: 'roles', scope: 'system' as const },
    ],
    created_at: '2024-01-01T00:00:00Z',
    updated_at: '2024-01-01T00:00:00Z',
}

const mockRoleBuiltin = {
    id: 'role-builtin-1',
    name: 'Admin',
    description: 'Built-in admin role',
    scope: 'system' as const,
    is_builtin: true,
    project_id: null,
    permissions: [],
    created_at: '2024-01-01T00:00:00Z',
    updated_at: '2024-01-01T00:00:00Z',
}

describe('SystemRoles', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        // Default: grant access
        mockHasSystemPermission.mockImplementation(() => true)
    })

    // -----------------------------------------------------------------------
    // 1. Access denied when no roles.manage permission
    // -----------------------------------------------------------------------
    it('renders access-denied UI when hasSystemPermission returns false', async () => {
        mockHasSystemPermission.mockImplementation(() => false)
        // listSystemRoles should not be called but mock it anyway to prevent hanging
        vi.mocked(permissionsApi.listSystemRoles).mockResolvedValue([])

        render(<SystemRoles />, { wrapper: createWrapper() })

        expect(await screen.findByText(/нет доступа/i)).toBeInTheDocument()
        expect(screen.getByText(/нет прав для управления системными ролями/i)).toBeInTheDocument()
        expect(screen.queryByRole('table')).not.toBeInTheDocument()
    })

    // -----------------------------------------------------------------------
    // 2. Renders roles table when permission is granted
    // -----------------------------------------------------------------------
    it('renders roles table when permission is granted', async () => {
        vi.mocked(permissionsApi.listSystemRoles).mockResolvedValue([mockRoleCustom, mockRoleBuiltin])

        render(<SystemRoles />, { wrapper: createWrapper() })

        expect(await screen.findByText('Custom Role')).toBeInTheDocument()
        expect(screen.getByText('Admin')).toBeInTheDocument()
        expect(screen.getByText('Системные роли')).toBeInTheDocument()
    })

    // -----------------------------------------------------------------------
    // 3. Search filters visible rows by name substring
    // -----------------------------------------------------------------------
    it('search filters visible rows by name substring', async () => {
        const user = userEvent.setup()
        vi.mocked(permissionsApi.listSystemRoles).mockResolvedValue([mockRoleCustom, mockRoleBuiltin])

        render(<SystemRoles />, { wrapper: createWrapper() })

        await screen.findByText('Custom Role')
        await screen.findByText('Admin')

        const searchInput = screen.getByPlaceholderText(/поиск по названию/i)
        await user.type(searchInput, 'Custom')

        // Admin should be hidden after filtering
        await waitFor(() => {
            expect(screen.queryByText('Admin')).not.toBeInTheDocument()
        })
        expect(screen.getByText('Custom Role')).toBeInTheDocument()
    })

    // -----------------------------------------------------------------------
    // 4. Create modal: empty name → notifyError, no API call
    // -----------------------------------------------------------------------
    it('shows error and does not call API when role name is empty on create', async () => {
        const user = userEvent.setup()
        vi.mocked(permissionsApi.listSystemRoles).mockResolvedValue([])

        render(<SystemRoles />, { wrapper: createWrapper() })

        await screen.findByText('Системные роли')

        // Open create modal
        await user.click(screen.getByRole('button', { name: /создать роль/i }))
        expect(await screen.findByRole('heading', { name: /создать роль/i })).toBeInTheDocument()

        // Submit without filling name
        await user.click(screen.getByRole('button', { name: /сохранить/i }))

        await waitFor(() => {
            expect(vi.mocked(notifyError)).toHaveBeenCalledWith('Название роли обязательно')
        })
        expect(vi.mocked(permissionsApi.createSystemRole)).not.toHaveBeenCalled()
    })

    // -----------------------------------------------------------------------
    // 5. Create modal: valid name → calls createSystemRole with trimmed payload
    // -----------------------------------------------------------------------
    it('calls createSystemRole with trimmed name on valid submit', async () => {
        const user = userEvent.setup()
        vi.mocked(permissionsApi.listSystemRoles).mockResolvedValue([])
        vi.mocked(permissionsApi.createSystemRole).mockResolvedValue({
            ...mockRoleCustom,
            name: 'New Role',
        })

        render(<SystemRoles />, { wrapper: createWrapper() })

        await screen.findByText('Системные роли')

        await user.click(screen.getByRole('button', { name: /создать роль/i }))
        await screen.findByRole('heading', { name: /создать роль/i })

        // Use fireEvent.change to avoid jsdom cssstyle bug with
        // border: 1px solid var(--outline) in form-group inputs on focus.
        // Surrounding whitespace is trimmed by handleSubmit.
        fireEvent.change(screen.getByLabelText(/название \*/i), {
            target: { value: '  New Role  ' },
        })
        await user.click(screen.getByRole('button', { name: /сохранить/i }))

        await waitFor(() => {
            expect(vi.mocked(permissionsApi.createSystemRole)).toHaveBeenCalledWith(
                expect.objectContaining({ name: 'New Role' })
            )
        })
    })

    // -----------------------------------------------------------------------
    // 6. Edit modal: pre-populates form; save calls updateSystemRole
    // -----------------------------------------------------------------------
    it('edit modal pre-populates form and calls updateSystemRole on save', async () => {
        const user = userEvent.setup()
        vi.mocked(permissionsApi.listSystemRoles).mockResolvedValue([mockRoleCustom])
        vi.mocked(permissionsApi.updateSystemRole).mockResolvedValue(mockRoleCustom)

        render(<SystemRoles />, { wrapper: createWrapper() })

        await screen.findByText('Custom Role')

        await user.click(screen.getByRole('button', { name: /редактировать/i }))

        // Form should be pre-populated
        const nameInput = await screen.findByLabelText(/название \*/i)
        expect((nameInput as HTMLInputElement).value).toBe('Custom Role')

        // Use fireEvent.change to avoid jsdom cssstyle bug with
        // border: 1px solid var(--outline) in form-group inputs on focus.
        fireEvent.change(nameInput, { target: { value: 'Renamed Role' } })
        await user.click(screen.getByRole('button', { name: /сохранить/i }))

        await waitFor(() => {
            expect(vi.mocked(permissionsApi.updateSystemRole)).toHaveBeenCalledWith(
                'role-custom-1',
                expect.objectContaining({ name: 'Renamed Role' })
            )
        })
    })

    // -----------------------------------------------------------------------
    // 7. Delete custom role; built-in role has no delete button
    // -----------------------------------------------------------------------
    it('delete custom role calls deleteSystemRole; built-in role has no delete button', async () => {
        const user = userEvent.setup()
        vi.stubGlobal('confirm', vi.fn(() => true))
        vi.mocked(permissionsApi.listSystemRoles).mockResolvedValue([mockRoleCustom, mockRoleBuiltin])
        vi.mocked(permissionsApi.deleteSystemRole).mockResolvedValue(undefined)

        render(<SystemRoles />, { wrapper: createWrapper() })

        await screen.findByText('Custom Role')
        await screen.findByText('Admin')

        // There should be only one delete button (for custom role, not built-in)
        const deleteButtons = screen.getAllByRole('button', { name: /удалить/i })
        expect(deleteButtons).toHaveLength(1)

        await user.click(deleteButtons[0])

        await waitFor(() => {
            expect(vi.mocked(permissionsApi.deleteSystemRole)).toHaveBeenCalledWith('role-custom-1')
        })
    })
})
