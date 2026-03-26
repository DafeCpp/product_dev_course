import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ProjectMembersModal from './ProjectMembersModal'
import { projectsApi, usersApi } from '../api/client'
import { authApi } from '../api/auth'
// Мокаем API
vi.mock('../api/client', () => ({
    projectsApi: {
        listMembers: vi.fn(),
        addMember: vi.fn(),
        removeMember: vi.fn(),
        updateMemberRole: vi.fn(),
    },
    usersApi: {
        search: vi.fn(),
    },
}))

vi.mock('../api/auth', () => ({
    authApi: {
        me: vi.fn(),
    },
}))

const createWrapper = () => {
    const queryClient = new QueryClient({
        defaultOptions: {
            queries: { retry: false },
            mutations: { retry: false },
        },
    })
    return ({ children }: { children: React.ReactNode }) => (
        <QueryClientProvider client={queryClient}>{children}</QueryClientProvider>
    )
}

describe('ProjectMembersModal', () => {
    const mockOnClose = vi.fn()
    const projectId = 'project-1'
    const projectOwnerId = 'owner-1'
    const currentUserId = 'owner-1'

    const mockMembers = [
        {
            project_id: projectId,
            user_id: 'owner-1',
            role: 'owner' as const,
            created_at: '2024-01-01T00:00:00Z',
            username: 'owner',
        },
        {
            project_id: projectId,
            user_id: 'editor-1',
            role: 'editor' as const,
            created_at: '2024-01-02T00:00:00Z',
            username: 'editor',
        },
        {
            project_id: projectId,
            user_id: 'viewer-1',
            role: 'viewer' as const,
            created_at: '2024-01-03T00:00:00Z',
            username: 'viewer',
        },
    ]

    const mockCurrentUser = {
        id: currentUserId,
        username: 'owner',
        email: 'owner@example.com',
        is_active: true,
        created_at: '2024-01-01T00:00:00Z',
    }

    beforeEach(() => {
        vi.clearAllMocks()
        mockOnClose.mockClear()

        const mockAuthApi = vi.mocked(authApi)
        mockAuthApi.me.mockResolvedValue(mockCurrentUser)

        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.listMembers.mockResolvedValue({
            members: mockMembers,
        })

        const mockUsersApi = vi.mocked(usersApi)
        mockUsersApi.search.mockResolvedValue({ users: [] })
    })

    it('does not render when isOpen is false', () => {
        render(
            <ProjectMembersModal
                isOpen={false}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        expect(screen.queryByRole('heading', { name: /участники проекта/i })).not.toBeInTheDocument()
    })

    it('renders modal when isOpen is true', async () => {
        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByRole('heading', { name: /участники проекта/i })).toBeInTheDocument()
        })
    })

    it('loads and displays members list', async () => {
        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(projectsApi.listMembers).toHaveBeenCalledWith(projectId)
        })

        await waitFor(() => {
            expect(screen.getByText('owner')).toBeInTheDocument()
            expect(screen.getByText('editor')).toBeInTheDocument()
            expect(screen.getByText('viewer')).toBeInTheDocument()
        })
    })

    it('shows loading state while fetching members', () => {
        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.listMembers.mockImplementation(
            () => new Promise(() => {}) // Never resolves
        )

        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        expect(screen.getByText(/загрузка/i)).toBeInTheDocument()
    })

    it('shows error message when loading members fails', async () => {
        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.listMembers.mockRejectedValueOnce(new Error('Failed to load members'))

        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText(/ошибка загрузки участников/i)).toBeInTheDocument()
        })
    })

    it('displays empty message when no members', async () => {
        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.listMembers.mockResolvedValueOnce({ members: [] })

        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText(/нет участников/i)).toBeInTheDocument()
        })
    })

    it('shows add member form for owner', async () => {
        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByRole('heading', { name: /добавить участника/i })).toBeInTheDocument()
            expect(screen.getByRole('combobox')).toBeInTheDocument()
            expect(screen.getByLabelText(/роль/i)).toBeInTheDocument()
        })
    })

    it('does not show add member form for non-owner', async () => {
        const nonOwnerId = 'editor-1'
        const mockAuthApi = vi.mocked(authApi)
        mockAuthApi.me.mockResolvedValueOnce({
            ...mockCurrentUser,
            id: nonOwnerId,
        })

        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.queryByText(/добавить участника/i)).not.toBeInTheDocument()
        })
    })

    it('searches users and selects from dropdown', async () => {
        const user = userEvent.setup()
        const mockUsersApi = vi.mocked(usersApi)
        mockUsersApi.search.mockResolvedValue({
            users: [{ id: 'new-user-1', username: 'newuser', email: 'new@example.com' }],
        })

        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.addMember.mockResolvedValueOnce({
            project_id: projectId,
            user_id: 'new-user-1',
            role: 'viewer' as const,
            created_at: '2024-01-04T00:00:00Z',
        })

        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByRole('combobox')).toBeInTheDocument()
        })

        const combobox = screen.getByRole('combobox')
        await user.type(combobox, 'new')

        await waitFor(() => {
            expect(screen.getByText('newuser')).toBeInTheDocument()
        })

        await user.click(screen.getByText('newuser'))

        const addButton = screen.getByRole('button', { name: /добавить участника/i })
        await user.click(addButton)

        await waitFor(() => {
            expect(mockProjectsApi.addMember).toHaveBeenCalledWith(projectId, {
                user_id: 'new-user-1',
                role: 'viewer',
            })
        })
    })

    it('updates member role successfully', async () => {
        const user = userEvent.setup()
        const mockProjectsApi = vi.mocked(projectsApi)
        const updatedMember = {
            project_id: projectId,
            user_id: 'editor-1',
            role: 'owner' as const,
            created_at: '2024-01-02T00:00:00Z',
        }
        mockProjectsApi.updateMemberRole.mockResolvedValueOnce(updatedMember)

        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('editor')).toBeInTheDocument()
        })

        const editorRow = screen.getByText('editor').closest('tr')
        expect(editorRow).toBeInTheDocument()

        const editorRoleSelect = editorRow?.querySelector('select') as HTMLSelectElement
        expect(editorRoleSelect).toBeInTheDocument()
        expect(editorRoleSelect.value).toBe('editor')

        await user.selectOptions(editorRoleSelect, 'owner')

        await waitFor(() => {
            expect(mockProjectsApi.updateMemberRole).toHaveBeenCalledWith(projectId, 'editor-1', {
                role: 'owner',
            })
        })
    })

    it('removes member successfully', async () => {
        const user = userEvent.setup()
        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.removeMember.mockResolvedValueOnce(undefined)

        const confirmSpy = vi.spyOn(window, 'confirm').mockReturnValue(true)

        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('editor')).toBeInTheDocument()
        })

        const removeButtons = screen.getAllByRole('button', { name: /удалить/i })
        const editorRemoveButton = removeButtons.find(
            (button) => button.closest('tr')?.textContent?.includes('editor')
        )

        expect(editorRemoveButton).toBeInTheDocument()
        await user.click(editorRemoveButton!)

        await waitFor(() => {
            expect(confirmSpy).toHaveBeenCalled()
            expect(mockProjectsApi.removeMember).toHaveBeenCalledWith(projectId, 'editor-1')
        })

        confirmSpy.mockRestore()
    })

    it('does not remove member if confirmation is cancelled', async () => {
        const user = userEvent.setup()
        const mockProjectsApi = vi.mocked(projectsApi)

        const confirmSpy = vi.spyOn(window, 'confirm').mockReturnValue(false)

        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('editor')).toBeInTheDocument()
        })

        const removeButtons = screen.getAllByRole('button', { name: /удалить/i })
        const editorRemoveButton = removeButtons.find(
            (button) => button.closest('tr')?.textContent?.includes('editor')
        )

        expect(editorRemoveButton).toBeInTheDocument()
        await user.click(editorRemoveButton!)

        await waitFor(() => {
            expect(confirmSpy).toHaveBeenCalled()
        })

        expect(mockProjectsApi.removeMember).not.toHaveBeenCalled()

        confirmSpy.mockRestore()
    })

    it('does not allow removing project owner', async () => {
        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('owner')).toBeInTheDocument()
        })

        const ownerRow = screen.getByText('owner').closest('tr')
        expect(ownerRow).toBeInTheDocument()
        expect(ownerRow).toHaveTextContent(/нельзя удалить/i)
    })

    it('displays badges for current user and project owner', async () => {
        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('owner')).toBeInTheDocument()
        })

        expect(screen.getByText('Вы')).toBeInTheDocument()
        expect(screen.getByText('Владелец проекта')).toBeInTheDocument()
    })

    it('closes modal when close button is clicked', async () => {
        const user = userEvent.setup()
        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByRole('heading', { name: /участники проекта/i })).toBeInTheDocument()
        })

        const closeButton = screen.getByRole('button', { name: /×/i })
        await user.click(closeButton)

        expect(mockOnClose).toHaveBeenCalledTimes(1)
    })

    it('add button is disabled when no user selected', async () => {
        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByRole('heading', { name: /добавить участника/i })).toBeInTheDocument()
        })

        const addButton = screen.getByRole('button', { name: /добавить участника/i })
        expect(addButton).toBeDisabled()
    })

    it('shows role labels correctly', async () => {
        render(
            <ProjectMembersModal
                isOpen={true}
                onClose={mockOnClose}
                projectId={projectId}
                projectOwnerId={projectOwnerId}
            />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('owner')).toBeInTheDocument()
            expect(screen.getByText('editor')).toBeInTheDocument()
            expect(screen.getByText('viewer')).toBeInTheDocument()
        })

        const ownerRow = screen.getByText('owner').closest('tr')
        expect(ownerRow).toBeInTheDocument()
        const ownerSelect = ownerRow?.querySelector('select')
        expect(ownerSelect).not.toBeInTheDocument()
        expect(ownerRow).toHaveTextContent(/владелец/i)

        const editorRow = screen.getByText('editor').closest('tr')
        expect(editorRow).toBeInTheDocument()
        const editorSelect = editorRow?.querySelector('select')
        expect(editorSelect).toBeInTheDocument()

        const viewerRow = screen.getByText('viewer').closest('tr')
        expect(viewerRow).toBeInTheDocument()
        const viewerSelect = viewerRow?.querySelector('select')
        expect(viewerSelect).toBeInTheDocument()
    })
})
