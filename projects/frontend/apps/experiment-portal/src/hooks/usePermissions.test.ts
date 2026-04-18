import { describe, it, expect, vi, beforeEach } from 'vitest'
import { renderHook, waitFor } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import React from 'react'
import { usePermissions } from './usePermissions'
import { authApi } from '../api/auth'
import { permissionsApi } from '../api/permissions'

vi.mock('../api/auth', () => ({
    authApi: {
        me: vi.fn(),
    },
}))

vi.mock('../api/permissions', () => ({
    permissionsApi: {
        getEffectivePermissions: vi.fn(),
    },
}))

vi.mock('../utils/activeProject', () => ({
    getActiveProjectId: vi.fn().mockReturnValue(null),
}))

const createWrapper = () => {
    const queryClient = new QueryClient({
        defaultOptions: {
            queries: { retry: false },
        },
    })
    return ({ children }: { children: React.ReactNode }) =>
        React.createElement(QueryClientProvider, { client: queryClient }, children)
}

const mockUser = {
    id: 'user-1',
    username: 'testuser',
    email: 'test@example.com',
    is_active: true,
    created_at: '2024-01-01T00:00:00Z',
}

describe('usePermissions', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('returns isLoading_true while effective permissions are being fetched', async () => {
        vi.mocked(authApi).me.mockResolvedValue(mockUser)
        vi.mocked(permissionsApi).getEffectivePermissions.mockImplementation(
            () => new Promise(() => {})
        )

        const { result } = renderHook(() => usePermissions(), {
            wrapper: createWrapper(),
        })

        await waitFor(() => {
            // me() resolved, permissions pending => isLoading should be true
            expect(result.current.isLoading).toBe(true)
        })
    })

    it('hasPermission_returns_true when permission is in project_permissions list', async () => {
        vi.mocked(authApi).me.mockResolvedValue(mockUser)
        vi.mocked(permissionsApi).getEffectivePermissions.mockResolvedValue({
            system_permissions: [],
            project_permissions: ['experiments.create', 'experiments.read'],
            is_superadmin: false,
        })

        const { result } = renderHook(() => usePermissions(), {
            wrapper: createWrapper(),
        })

        // Wait for actual data — isLoading alone may flip early before me() resolves
        await waitFor(() => {
            expect(result.current.permissions).toContain('experiments.create')
        })

        expect(result.current.hasPermission('experiments.create')).toBe(true)
    })

    it('hasPermission_returns_false when permission is absent from all lists', async () => {
        vi.mocked(authApi).me.mockResolvedValue(mockUser)
        vi.mocked(permissionsApi).getEffectivePermissions.mockResolvedValue({
            system_permissions: [],
            project_permissions: ['experiments.read'],
            is_superadmin: false,
        })

        const { result } = renderHook(() => usePermissions(), {
            wrapper: createWrapper(),
        })

        await waitFor(() => {
            expect(result.current.isLoading).toBe(false)
        })

        expect(result.current.hasPermission('experiments.create')).toBe(false)
    })

    it('hasPermission_returns_true for any permission when isSuperadmin is true', async () => {
        vi.mocked(authApi).me.mockResolvedValue(mockUser)
        vi.mocked(permissionsApi).getEffectivePermissions.mockResolvedValue({
            system_permissions: [],
            project_permissions: [],
            is_superadmin: true,
        })

        const { result } = renderHook(() => usePermissions(), {
            wrapper: createWrapper(),
        })

        await waitFor(() => {
            expect(result.current.isSuperadmin).toBe(true)
        })

        expect(result.current.hasPermission('experiments.create')).toBe(true)
        expect(result.current.hasPermission('any.arbitrary.permission')).toBe(true)
    })

    it('hasSystemPermission_returns_true when permission is in system_permissions list', async () => {
        vi.mocked(authApi).me.mockResolvedValue(mockUser)
        vi.mocked(permissionsApi).getEffectivePermissions.mockResolvedValue({
            system_permissions: ['audit.read', 'roles.assign'],
            project_permissions: [],
            is_superadmin: false,
        })

        const { result } = renderHook(() => usePermissions(), {
            wrapper: createWrapper(),
        })

        await waitFor(() => {
            expect(result.current.systemPermissions).toContain('audit.read')
        })

        expect(result.current.hasSystemPermission('audit.read')).toBe(true)
    })

    it('hasSystemPermission_returns_false when permission is only in project_permissions', async () => {
        vi.mocked(authApi).me.mockResolvedValue(mockUser)
        vi.mocked(permissionsApi).getEffectivePermissions.mockResolvedValue({
            system_permissions: [],
            project_permissions: ['audit.read'],
            is_superadmin: false,
        })

        const { result } = renderHook(() => usePermissions(), {
            wrapper: createWrapper(),
        })

        await waitFor(() => {
            expect(result.current.isLoading).toBe(false)
        })

        // audit.read exists only in project_permissions — hasSystemPermission must not cross-check
        expect(result.current.hasSystemPermission('audit.read')).toBe(false)
    })

    it('hasSystemPermission_returns_true for any permission when isSuperadmin is true', async () => {
        vi.mocked(authApi).me.mockResolvedValue(mockUser)
        vi.mocked(permissionsApi).getEffectivePermissions.mockResolvedValue({
            system_permissions: [],
            project_permissions: [],
            is_superadmin: true,
        })

        const { result } = renderHook(() => usePermissions(), {
            wrapper: createWrapper(),
        })

        await waitFor(() => {
            expect(result.current.isSuperadmin).toBe(true)
        })

        expect(result.current.hasSystemPermission('audit.read')).toBe(true)
    })

    it('returns isLoading_false and empty permissions when user is not authenticated', async () => {
        vi.mocked(authApi).me.mockRejectedValue(new Error('Unauthorized'))

        const { result } = renderHook(() => usePermissions(), {
            wrapper: createWrapper(),
        })

        await waitFor(() => {
            expect(result.current.isLoading).toBe(false)
        })

        expect(result.current.permissions).toEqual([])
        expect(result.current.systemPermissions).toEqual([])
        expect(result.current.isSuperadmin).toBe(false)
    })
})
