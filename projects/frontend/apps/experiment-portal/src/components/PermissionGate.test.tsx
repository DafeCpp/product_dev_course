import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen } from '@testing-library/react'
import PermissionGate from './PermissionGate'

const mockHasPermission = vi.fn()
const mockHasSystemPermission = vi.fn()

vi.mock('../hooks/usePermissions', () => ({
    usePermissions: () => ({
        hasPermission: mockHasPermission,
        hasSystemPermission: mockHasSystemPermission,
        permissions: [],
        systemPermissions: [],
        isSuperadmin: false,
        isLoading: false,
    }),
}))

describe('PermissionGate', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('renders_children when hasPermission returns true', () => {
        mockHasPermission.mockReturnValue(true)

        render(
            <PermissionGate permission="experiments.create">
                <span>Protected content</span>
            </PermissionGate>
        )

        expect(screen.getByText('Protected content')).toBeInTheDocument()
        expect(mockHasPermission).toHaveBeenCalledWith('experiments.create')
    })

    it('does_not_render_children when hasPermission returns false', () => {
        mockHasPermission.mockReturnValue(false)

        render(
            <PermissionGate permission="experiments.create">
                <span>Protected content</span>
            </PermissionGate>
        )

        expect(screen.queryByText('Protected content')).not.toBeInTheDocument()
    })

    it('renders_fallback when access is denied and fallback prop is provided', () => {
        mockHasPermission.mockReturnValue(false)

        render(
            <PermissionGate
                permission="experiments.create"
                fallback={<span>No access</span>}
            >
                <span>Protected content</span>
            </PermissionGate>
        )

        expect(screen.queryByText('Protected content')).not.toBeInTheDocument()
        expect(screen.getByText('No access')).toBeInTheDocument()
    })

    it('does_not_render_fallback when access is granted', () => {
        mockHasPermission.mockReturnValue(true)

        render(
            <PermissionGate
                permission="experiments.create"
                fallback={<span>No access</span>}
            >
                <span>Protected content</span>
            </PermissionGate>
        )

        expect(screen.getByText('Protected content')).toBeInTheDocument()
        expect(screen.queryByText('No access')).not.toBeInTheDocument()
    })

    it('uses_hasSystemPermission when system prop is true', () => {
        mockHasSystemPermission.mockReturnValue(true)
        mockHasPermission.mockReturnValue(false)

        render(
            <PermissionGate permission="audit.read" system={true}>
                <span>System content</span>
            </PermissionGate>
        )

        expect(screen.getByText('System content')).toBeInTheDocument()
        expect(mockHasSystemPermission).toHaveBeenCalledWith('audit.read')
        expect(mockHasPermission).not.toHaveBeenCalled()
    })

    it('uses_hasPermission_not_hasSystemPermission when system prop is false', () => {
        mockHasPermission.mockReturnValue(true)
        mockHasSystemPermission.mockReturnValue(false)

        render(
            <PermissionGate permission="experiments.create" system={false}>
                <span>Project content</span>
            </PermissionGate>
        )

        expect(screen.getByText('Project content')).toBeInTheDocument()
        expect(mockHasPermission).toHaveBeenCalledWith('experiments.create')
        expect(mockHasSystemPermission).not.toHaveBeenCalled()
    })

    it('hides_children_via_system_check when system permission is denied', () => {
        mockHasSystemPermission.mockReturnValue(false)

        render(
            <PermissionGate
                permission="audit.read"
                system={true}
                fallback={<span>Access denied</span>}
            >
                <span>Audit content</span>
            </PermissionGate>
        )

        expect(screen.queryByText('Audit content')).not.toBeInTheDocument()
        expect(screen.getByText('Access denied')).toBeInTheDocument()
    })
})
