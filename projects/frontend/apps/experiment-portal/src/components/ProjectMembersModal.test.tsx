import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ProjectMembersModal from './ProjectMembersModal'
import { projectsApi } from '../api/client'
import { authApi } from '../api/auth'

// Мокаем API
vi.mock('../api/client', () => ({
    projectsApi: {
        listMembers: vi.fn(),
        addMember: vi.fn(),
        removeMember: vi.fn(),
        updateMemberRole: vi.fn(),
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

        // Мокаем authApi.me
        const mockAuthApi = vi.mocked(authApi)
        mockAuthApi.me.mockResolvedValue(mockCurrentUser)

        // Мокаем projectsApi.listMembers
        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.listMembers.mockResolvedValue({
            members: mockMembers,
        })
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

        // Проверяем, что отображается индикатор загрузки
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
            expect(screen.getByLabelText(/id пользователя/i)).toBeInTheDocument()
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

    it('adds new member successfully', async () => {
        const user = userEvent.setup()
        const mockProjectsApi = vi.mocked(projectsApi)
        const newMember = {
            project_id: projectId,
            user_id: 'new-user-1',
            role: 'viewer' as const,
            created_at: '2024-01-04T00:00:00Z',
        }
        mockProjectsApi.addMember.mockResolvedValueOnce(newMember)

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
            expect(screen.getByLabelText(/id пользователя/i)).toBeInTheDocument()
        })

        const userIdInput = screen.getByLabelText(/id пользователя/i)
        await user.type(userIdInput, 'new-user-1')

        const roleSelect = screen.getByLabelText(/роль/i)
        await user.selectOptions(roleSelect, 'viewer')

        const addButton = screen.getByRole('button', { name: /добавить участника/i })
        await user.click(addButton)

        await waitFor(() => {
            expect(mockProjectsApi.addMember).toHaveBeenCalledWith(projectId, {
                user_id: 'new-user-1',
                role: 'viewer',
            })
        })

        // Проверяем, что список обновился
        await waitFor(() => {
            expect(mockProjectsApi.listMembers).toHaveBeenCalledTimes(2) // Initial + after add
        })
    })

    it('shows error when adding member fails', async () => {
        const user = userEvent.setup()
        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.addMember.mockRejectedValueOnce({
            response: {
                data: { error: 'User not found' },
            },
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
            expect(screen.getByLabelText(/id пользователя/i)).toBeInTheDocument()
        })

        const userIdInput = screen.getByLabelText(/id пользователя/i)
        await user.type(userIdInput, 'invalid-user')

        const addButton = screen.getByRole('button', { name: /добавить участника/i })
        await user.click(addButton)

        await waitFor(() => {
            expect(screen.getByText(/user not found/i)).toBeInTheDocument()
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

        // Находим select для роли editor - ищем в строке таблицы
        const editorRow = screen.getByText('editor').closest('tr')
        expect(editorRow).toBeInTheDocument()

        // Ищем select в строке editor
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

        // Мокаем window.confirm
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

        // Находим кнопку удаления для editor
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

        // Мокаем window.confirm
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

        // Проверяем, что для owner отображается "Нельзя удалить"
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

        // Проверяем наличие бейджей
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

    it('validates required user ID field when adding member', async () => {
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
            expect(screen.getByLabelText(/id пользователя/i)).toBeInTheDocument()
        })

        const addButton = screen.getByRole('button', { name: /добавить участника/i })
        await user.click(addButton)

        // HTML5 validation should prevent submission
        const userIdInput = screen.getByLabelText(/id пользователя/i) as HTMLInputElement
        expect(userIdInput.validity.valueMissing).toBe(true)
    })

    it('disables form during operations', async () => {
        const user = userEvent.setup()
        const mockProjectsApi = vi.mocked(projectsApi)

        // Мокаем долгий запрос
        let resolveAdd: (value: any) => void
        const addPromise = new Promise((resolve) => {
            resolveAdd = resolve
        })
        mockProjectsApi.addMember.mockReturnValueOnce(addPromise as any)

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
            expect(screen.getByLabelText(/id пользователя/i)).toBeInTheDocument()
        })

        const userIdInput = screen.getByLabelText(/id пользователя/i)
        await user.type(userIdInput, 'new-user-1')

        const addButton = screen.getByRole('button', { name: /добавить участника/i })
        const clickPromise = user.click(addButton)

        // Проверяем, что форма заблокирована
        await waitFor(() => {
            expect(addButton).toBeDisabled()
            expect(userIdInput).toBeDisabled()
        })

        // Завершаем промис
        resolveAdd!({
            project_id: projectId,
            user_id: 'new-user-1',
            role: 'viewer' as const,
            created_at: '2024-01-04T00:00:00Z',
        })

        await clickPromise
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

        // Проверяем, что роли отображаются правильно
        // Для owner участника (который является владельцем проекта) должен быть текст (не select)
        const ownerRow = screen.getByText('owner').closest('tr')
        expect(ownerRow).toBeInTheDocument()
        const ownerSelect = ownerRow?.querySelector('select')
        expect(ownerSelect).not.toBeInTheDocument() // Select не должен быть для владельца проекта
        expect(ownerRow).toHaveTextContent(/владелец/i) // Должен быть текст с ролью

        // Проверяем, что для editor есть select (так как он не владелец проекта и текущий пользователь - owner)
        const editorRow = screen.getByText('editor').closest('tr')
        expect(editorRow).toBeInTheDocument()
        const editorSelect = editorRow?.querySelector('select')
        expect(editorSelect).toBeInTheDocument()

        // Проверяем, что для viewer тоже есть select
        const viewerRow = screen.getByText('viewer').closest('tr')
        expect(viewerRow).toBeInTheDocument()
        const viewerSelect = viewerRow?.querySelector('select')
        expect(viewerSelect).toBeInTheDocument()
    })
})

