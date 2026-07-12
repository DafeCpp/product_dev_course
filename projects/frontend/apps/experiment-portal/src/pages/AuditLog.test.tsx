import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import AuditLog from './AuditLog'
import { auditApi } from '../api/audit'

vi.mock('../api/audit', () => ({
    auditApi: {
        queryAuditLog: vi.fn(),
    },
}))

const mockHasSystemPermission = vi.fn()
let mockPermissionsLoading = false

vi.mock('../hooks/usePermissions', () => ({
    usePermissions: () => ({
        hasSystemPermission: mockHasSystemPermission,
        hasPermission: vi.fn(() => true),
        hasAnyPermission: vi.fn(() => true),
        isSuperadmin: false,
        systemPermissions: [],
        projectPermissions: [],
        permissions: [],
        isLoading: mockPermissionsLoading,
    }),
}))

let searchParamsString = ''
const mockSetSearchParams = vi.fn((params: any) => {
    if (typeof params === 'object' && params !== null) {
        searchParamsString = new URLSearchParams(params).toString()
    } else {
        searchParamsString = ''
    }
})

vi.mock('react-router-dom', async () => {
    const actual = await vi.importActual<typeof import('react-router-dom')>('react-router-dom')
    return {
        ...actual,
        useSearchParams: () => [new URLSearchParams(searchParamsString), mockSetSearchParams],
    }
})

vi.mock('../utils/notify', () => ({
    notifyError: vi.fn(),
    notifySuccess: vi.fn(),
}))

const createWrapper = () => {
    const queryClient = new QueryClient({
        defaultOptions: {
            queries: { retry: false },
            mutations: { retry: false },
        },
    })
    return ({ children }: { children: React.ReactNode }) => (
        <QueryClientProvider client={queryClient}>
            <MemoryRouter>{children}</MemoryRouter>
        </QueryClientProvider>
    )
}

const mockEntry = {
    id: 'entry-1',
    actor_id: 'user-1',
    actor_username: 'alice',
    action: 'user.login',
    scope_type: 'system',
    scope_id: null,
    target_type: null,
    target_id: null,
    ip_address: '127.0.0.1',
    details: { extra: 'data' },
    created_at: '2026-05-08T12:00:00Z',
}

describe('AuditLog', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        searchParamsString = ''
        mockPermissionsLoading = false
        mockHasSystemPermission.mockImplementation(() => true)
    })

    it('shows loading indicator while permissions are loading', () => {
        mockPermissionsLoading = true
        render(<AuditLog />, { wrapper: createWrapper() })
        expect(screen.getByText(/проверка прав доступа/i)).toBeInTheDocument()
    })

    it('shows no-access UI when audit.read permission is missing', () => {
        mockHasSystemPermission.mockImplementation((perm: string) => perm !== 'audit.read')
        render(<AuditLog />, { wrapper: createWrapper() })
        expect(screen.getByText(/нет доступа/i)).toBeInTheDocument()
        expect(vi.mocked(auditApi.queryAuditLog)).not.toHaveBeenCalled()
    })

    it('renders title and filter controls when access is granted', async () => {
        vi.mocked(auditApi.queryAuditLog).mockResolvedValueOnce({ entries: [], total: 0, limit: 50, offset: 0 })
        render(<AuditLog />, { wrapper: createWrapper() })

        expect(screen.getByRole('heading', { name: /аудит-лог/i })).toBeInTheDocument()
        expect(screen.getByLabelText(/фильтр по действию/i)).toBeInTheDocument()
        expect(screen.getByLabelText(/фильтр по пользователю/i)).toBeInTheDocument()
        expect(screen.getByLabelText(/дата с/i)).toBeInTheDocument()
        expect(screen.getByLabelText(/дата по/i)).toBeInTheDocument()

        await waitFor(() => {
            expect(vi.mocked(auditApi.queryAuditLog)).toHaveBeenCalled()
        })
    })

    it('renders empty state when no entries returned', async () => {
        vi.mocked(auditApi.queryAuditLog).mockResolvedValueOnce({ entries: [], total: 0, limit: 50, offset: 0 })
        render(<AuditLog />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/записей аудит-лога не найдено/i)).toBeInTheDocument()
        })
    })

    it('renders entries in the table', async () => {
        vi.mocked(auditApi.queryAuditLog).mockResolvedValueOnce({
            entries: [mockEntry],
            total: 1,
            limit: 50,
            offset: 0,
        })
        render(<AuditLog />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('alice')).toBeInTheDocument()
            expect(screen.getByText('user.login')).toBeInTheDocument()
            expect(screen.getByText('127.0.0.1')).toBeInTheDocument()
        })
    })

    it('applies filters via setSearchParams when "Применить" is clicked', async () => {
        vi.mocked(auditApi.queryAuditLog).mockResolvedValue({ entries: [], total: 0, limit: 50, offset: 0 })
        const user = userEvent.setup()
        render(<AuditLog />, { wrapper: createWrapper() })

        const actionInput = screen.getByLabelText(/фильтр по действию/i)
        await user.type(actionInput, 'user.login')
        await user.click(screen.getByRole('button', { name: /применить/i }))

        expect(mockSetSearchParams).toHaveBeenCalledWith({ action: 'user.login' })
    })

    it('resets filters when "Сбросить" is clicked', async () => {
        searchParamsString = 'action=user.login&actor_id=u1'
        vi.mocked(auditApi.queryAuditLog).mockResolvedValue({ entries: [], total: 0, limit: 50, offset: 0 })
        const user = userEvent.setup()
        render(<AuditLog />, { wrapper: createWrapper() })

        await user.click(screen.getByRole('button', { name: /сбросить/i }))

        expect(mockSetSearchParams).toHaveBeenCalledWith({})
    })

    it('opens detail modal on row click', async () => {
        vi.mocked(auditApi.queryAuditLog).mockResolvedValueOnce({
            entries: [mockEntry],
            total: 1,
            limit: 50,
            offset: 0,
        })
        const user = userEvent.setup()
        render(<AuditLog />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('alice')).toBeInTheDocument()
        })

        await user.click(screen.getByText('user.login'))

        await waitFor(() => {
            expect(screen.getByRole('heading', { name: /детали записи аудита/i })).toBeInTheDocument()
        })
    })
})
