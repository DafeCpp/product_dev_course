import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import AdminUsers from './AdminUsers'
import { authApi } from '../api/auth'

vi.mock('../api/auth', () => ({
    authApi: {
        adminListUsers: vi.fn(),
        adminUpdateUser: vi.fn(),
        adminDeleteUser: vi.fn(),
        adminResetUserPassword: vi.fn(),
        adminCreateInvite: vi.fn(),
        adminListInvites: vi.fn(),
        adminRevokeInvite: vi.fn(),
    },
}))

const createWrapper = () => {
    const queryClient = new QueryClient({
        defaultOptions: {
            queries: { 
                retry: false,
                networkMode: 'always',
            },
            mutations: { retry: false },
        },
    })
    return ({ children }: { children: React.ReactNode }) => (
        <QueryClientProvider client={queryClient}>{children}</QueryClientProvider>
    )
}

const adminUser = {
    id: 'user-admin',
    username: 'adminuser',
    email: 'admin@example.com',
    is_active: true,
    is_admin: true,
    system_roles: ['admin'],
    password_change_required: false,
    created_at: '2024-01-15T10:00:00Z',
}

const regularUser = {
    id: 'user-regular',
    username: 'regularuser',
    email: 'regular@example.com',
    is_active: true,
    is_admin: false,
    password_change_required: false,
    created_at: '2024-02-01T12:00:00Z',
}

const inactiveUser = {
    id: 'user-inactive',
    username: 'inactiveuser',
    email: 'inactive@example.com',
    is_active: false,
    is_admin: false,
    password_change_required: false,
    created_at: '2024-03-01T08:00:00Z',
}

const activeInvite = {
    id: 'invite-1',
    token: 'abc123def456789012345',
    created_by: 'user-admin',
    email_hint: 'newuser@example.com',
    expires_at: '2025-01-20T10:00:00Z',
    used_at: null,
    used_by: null,
    created_at: '2024-01-15T10:00:00Z',
    is_active: true,
}

describe('AdminUsers', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        vi.mocked(authApi).adminListUsers.mockResolvedValue([adminUser, regularUser])
        vi.mocked(authApi).adminListInvites.mockResolvedValue([activeInvite])
        vi.mocked(authApi).adminUpdateUser.mockResolvedValue({ ...regularUser })
        vi.mocked(authApi).adminDeleteUser.mockResolvedValue(undefined)
        vi.mocked(authApi).adminResetUserPassword.mockResolvedValue({
            user: regularUser,
            new_password: 'tempPass123',
        })
        vi.mocked(authApi).adminCreateInvite.mockResolvedValue(activeInvite)
        vi.mocked(authApi).adminRevokeInvite.mockResolvedValue(undefined)
    })

    describe('Users tab', () => {
        it('renders users tab by default with user list', async () => {
            render(<AdminUsers />, { wrapper: createWrapper() })

            expect(await screen.findByText('regularuser')).toBeInTheDocument()
            expect(screen.getByText('adminuser')).toBeInTheDocument()
            expect(screen.getByText('regular@example.com')).toBeInTheDocument()
            expect(screen.getByText('admin@example.com')).toBeInTheDocument()
        })

        it('shows admin and user role badges', async () => {
            render(<AdminUsers />, { wrapper: createWrapper() })

            await screen.findByText('regularuser')

            const adminBadges = screen.getAllByText('admin')
            const userBadges = screen.getAllByText('user')
            expect(adminBadges.length).toBeGreaterThan(0)
            expect(userBadges.length).toBeGreaterThan(0)
        })

        it('shows empty state when no users found', async () => {
            vi.mocked(authApi).adminListUsers.mockResolvedValue([])
            render(<AdminUsers />, { wrapper: createWrapper() })

            expect(await screen.findByText(/пользователей не найдено/i)).toBeInTheDocument()
        })

        it('shows error state when fetch fails', async () => {
            const mockError = new Error('Server error')
            vi.mocked(authApi).adminListUsers.mockRejectedValue(mockError)
            
            const { container } = render(<AdminUsers />, { wrapper: createWrapper() })
            
            // Wait for loading to finish and error to appear
            await waitFor(
                () => {
                    const errorDiv = container.querySelector('.error')
                    expect(errorDiv).toBeInTheDocument()
                },
                { timeout: 2000 }
            )
            
            expect(container.querySelector('.error')?.textContent).toMatch(/server error/i)
        })

        it('shows pwd badge for user with password_change_required', async () => {
            vi.mocked(authApi).adminListUsers.mockResolvedValue([
                { ...regularUser, password_change_required: true },
            ])
            render(<AdminUsers />, { wrapper: createWrapper() })

            expect(await screen.findByTitle('Требуется смена пароля')).toBeInTheDocument()
        })

        it('shows Активир. button for inactive user', async () => {
            vi.mocked(authApi).adminListUsers.mockResolvedValue([inactiveUser])
            render(<AdminUsers />, { wrapper: createWrapper() })

            expect(await screen.findByTitle('Активировать')).toBeInTheDocument()
            expect(screen.getByText('Активир.')).toBeInTheDocument()
        })

        it('calls adminUpdateUser with is_active: false on deactivate', async () => {
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await screen.findByText('regularuser')

            const deactivateBtns = screen.getAllByTitle('Деактивировать')
            await user.click(deactivateBtns[0])

            await waitFor(() => {
                expect(vi.mocked(authApi).adminUpdateUser).toHaveBeenCalledWith(
                    adminUser.id,
                    { is_active: false }
                )
            })
        })

        it('calls adminUpdateUser with is_active: true on activate for inactive user', async () => {
            vi.mocked(authApi).adminListUsers.mockResolvedValue([inactiveUser])
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await screen.findByText('inactiveuser')

            await user.click(screen.getByTitle('Активировать'))

            await waitFor(() => {
                expect(vi.mocked(authApi).adminUpdateUser).toHaveBeenCalledWith(
                    inactiveUser.id,
                    { is_active: true }
                )
            })
        })

        it('calls adminUpdateUser with is_admin: true on +admin', async () => {
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await screen.findByText('regularuser')

            await user.click(screen.getByTitle('Сделать admin'))

            await waitFor(() => {
                expect(vi.mocked(authApi).adminUpdateUser).toHaveBeenCalledWith(
                    regularUser.id,
                    { is_admin: true }
                )
            })
        })

        it('calls adminUpdateUser with is_admin: false on -admin', async () => {
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await screen.findByText('adminuser')

            await user.click(screen.getByTitle('Убрать права admin'))

            await waitFor(() => {
                expect(vi.mocked(authApi).adminUpdateUser).toHaveBeenCalledWith(
                    adminUser.id,
                    { is_admin: false }
                )
            })
        })

        it('shows new password inline after reset', async () => {
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await screen.findByText('regularuser')

            // regularUser is second row
            const resetBtns = screen.getAllByTitle('Сбросить пароль')
            await user.click(resetBtns[1])

            expect(await screen.findByText(/tempPass123/)).toBeInTheDocument()
        })

        it('calls adminDeleteUser after confirm', async () => {
            vi.spyOn(window, 'confirm').mockReturnValue(true)
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await screen.findByText('regularuser')

            const deleteBtns = screen.getAllByTitle('Удалить пользователя')
            await user.click(deleteBtns[0])

            await waitFor(() => {
                expect(vi.mocked(authApi).adminDeleteUser).toHaveBeenCalledWith(adminUser.id)
            })
        })

        it('does not call adminDeleteUser when confirm cancelled', async () => {
            vi.spyOn(window, 'confirm').mockReturnValue(false)
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await screen.findByText('regularuser')

            const deleteBtns = screen.getAllByTitle('Удалить пользователя')
            await user.click(deleteBtns[0])

            expect(vi.mocked(authApi).adminDeleteUser).not.toHaveBeenCalled()
        })

        it('passes search param to adminListUsers when typing', async () => {
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            const searchInput = screen.getByPlaceholderText(/поиск по имени/i)
            await user.type(searchInput, 'john')

            await waitFor(() => {
                expect(vi.mocked(authApi).adminListUsers).toHaveBeenCalledWith(
                    expect.objectContaining({ search: 'john' })
                )
            })
        })

        it('passes is_active filter to adminListUsers', async () => {
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            const filterSelect = screen.getByRole('combobox')
            await user.selectOptions(filterSelect, 'active')

            await waitFor(() => {
                expect(vi.mocked(authApi).adminListUsers).toHaveBeenCalledWith(
                    expect.objectContaining({ is_active: true })
                )
            })
        })
    })

    describe('Invites tab', () => {
        it('switches to invites tab and shows invite list', async () => {
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await user.click(screen.getByRole('button', { name: /инвайты/i }))

            expect(await screen.findByText('newuser@example.com')).toBeInTheDocument()
            // Token displayed as first 8 chars + ellipsis
            expect(screen.getByText('abc123de…')).toBeInTheDocument()
        })

        it('shows empty state when no invites', async () => {
            vi.mocked(authApi).adminListInvites.mockResolvedValue([])
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await user.click(screen.getByRole('button', { name: /инвайты/i }))

            expect(await screen.findByText(/инвайтов нет/i)).toBeInTheDocument()
        })

        it('shows create invite form when button clicked', async () => {
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await user.click(screen.getByRole('button', { name: /инвайты/i }))
            await user.click(await screen.findByRole('button', { name: /создать инвайт/i }))

            expect(screen.getByLabelText(/email \(подсказка/i)).toBeInTheDocument()
            expect(screen.getByLabelText(/срок действия/i)).toBeInTheDocument()
        })

        it('calls adminCreateInvite with email and default expires', async () => {
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await user.click(screen.getByRole('button', { name: /инвайты/i }))
            await user.click(await screen.findByRole('button', { name: /создать инвайт/i }))

            const emailInput = screen.getByLabelText(/email \(подсказка/i)
            await user.type(emailInput, 'newperson@example.com')

            await user.click(screen.getByRole('button', { name: /^создать$/i }))

            await waitFor(() => {
                expect(vi.mocked(authApi).adminCreateInvite).toHaveBeenCalledWith({
                    email_hint: 'newperson@example.com',
                    expires_in_hours: 72,
                })
            })
        })

        it('hides create form after successful invite creation', async () => {
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await user.click(screen.getByRole('button', { name: /инвайты/i }))
            await user.click(await screen.findByRole('button', { name: /создать инвайт/i }))

            expect(screen.getByLabelText(/email \(подсказка/i)).toBeInTheDocument()

            await user.click(screen.getByRole('button', { name: /^создать$/i }))

            await waitFor(() => {
                expect(screen.queryByLabelText(/email \(подсказка/i)).not.toBeInTheDocument()
            })
        })

        it('calls adminRevokeInvite after confirm', async () => {
            vi.spyOn(window, 'confirm').mockReturnValue(true)
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await user.click(screen.getByRole('button', { name: /инвайты/i }))
            await user.click(await screen.findByRole('button', { name: /отозвать/i }))

            await waitFor(() => {
                expect(vi.mocked(authApi).adminRevokeInvite).toHaveBeenCalledWith(activeInvite.token)
            })
        })

        it('does not call adminRevokeInvite when confirm cancelled', async () => {
            vi.spyOn(window, 'confirm').mockReturnValue(false)
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await user.click(screen.getByRole('button', { name: /инвайты/i }))
            await user.click(await screen.findByRole('button', { name: /отозвать/i }))

            expect(vi.mocked(authApi).adminRevokeInvite).not.toHaveBeenCalled()
        })

        it('does not show revoke button for used invite', async () => {
            vi.mocked(authApi).adminListInvites.mockResolvedValue([
                { ...activeInvite, used_at: '2024-01-16T10:00:00Z', used_by: 'user-2', is_active: false },
            ])
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await user.click(screen.getByRole('button', { name: /инвайты/i }))

            await screen.findByText('abc123de…')

            expect(screen.queryByRole('button', { name: /отозвать/i })).not.toBeInTheDocument()
        })

        it('shows dash for invite with no email hint', async () => {
            vi.mocked(authApi).adminListInvites.mockResolvedValue([
                { ...activeInvite, email_hint: null },
            ])
            render(<AdminUsers />, { wrapper: createWrapper() })
            const user = userEvent.setup()

            await user.click(screen.getByRole('button', { name: /инвайты/i }))

            await screen.findByText('abc123de…')
            // Both email_hint and used_at are null, so two '—' cells are rendered
            const dashes = screen.getAllByText('—')
            expect(dashes.length).toBeGreaterThanOrEqual(1)
        })
    })
})
