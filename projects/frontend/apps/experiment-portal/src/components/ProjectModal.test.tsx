import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ProjectModal from './ProjectModal'
import { projectsApi } from '../api/client'
import { authApi } from '../api/auth'

vi.mock('../api/client', () => ({
    projectsApi: {
        get: vi.fn(),
        create: vi.fn(),
        update: vi.fn(),
        listMembers: vi.fn(),
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

describe('ProjectModal', () => {
    const projectId = 'project-1'
    const mockOnClose = vi.fn()

    const mockProject = {
        id: projectId,
        name: 'Project A',
        description: 'Desc',
        owner_id: 'owner-1',
        created_at: '2024-01-01T00:00:00Z',
        updated_at: '2024-01-01T00:00:00Z',
    }

    beforeEach(() => {
        vi.clearAllMocks()
        mockOnClose.mockClear()
    })

    it('creates a project in create mode', async () => {
        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.create.mockResolvedValue({
            ...mockProject,
            id: 'new-project',
        } as any)

        render(<ProjectModal isOpen={true} onClose={mockOnClose} mode="create" />, {
            wrapper: createWrapper(),
        })

        // Заголовок и кнопка имеют одинаковый текст, поэтому проверяем по роли заголовка.
        expect(await screen.findByRole('heading', { name: /создать проект/i })).toBeInTheDocument()

        const user = userEvent.setup()
        const nameInput = (await screen.findByLabelText(/название/i)) as HTMLInputElement
        await user.type(nameInput, 'New Project')

        await user.click(screen.getByRole('button', { name: /^создать проект$/i }))

        await waitFor(() => {
            expect(mockProjectsApi.create).toHaveBeenCalledWith({
                name: 'New Project',
                description: undefined,
            })
        })
    })

    it('renders read-only for viewer in view mode', async () => {
        const mockAuthApi = vi.mocked(authApi)
        mockAuthApi.me.mockResolvedValue({
            id: 'viewer-1',
            username: 'viewer',
            email: 'viewer@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })

        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.get.mockResolvedValue(mockProject as any)
        mockProjectsApi.listMembers.mockResolvedValue({
            members: [{ project_id: projectId, user_id: 'viewer-1', role: 'viewer', created_at: '2024-01-01T00:00:00Z' }],
        } as any)

        render(<ProjectModal isOpen={true} onClose={mockOnClose} mode="view" projectId={projectId} />, {
            wrapper: createWrapper(),
        })

        expect(await screen.findByText(/проект: просмотр/i)).toBeInTheDocument()

        const nameInput = (await screen.findByLabelText(/название/i)) as HTMLInputElement
        expect(nameInput.value).toBe('Project A')
        expect(nameInput).toBeDisabled()
        expect(screen.queryByRole('button', { name: /сохранить/i })).not.toBeInTheDocument()
    })

    it('allows editing for owner in edit mode and calls update', async () => {
        const mockAuthApi = vi.mocked(authApi)
        mockAuthApi.me.mockResolvedValue({
            id: 'owner-1',
            username: 'owner',
            email: 'owner@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })

        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.get.mockResolvedValue(mockProject as any)
        mockProjectsApi.listMembers.mockResolvedValue({
            members: [{ project_id: projectId, user_id: 'owner-1', role: 'owner', created_at: '2024-01-01T00:00:00Z' }],
        } as any)
        mockProjectsApi.update.mockResolvedValue({
            ...mockProject,
            name: 'Project Updated',
        } as any)

        render(<ProjectModal isOpen={true} onClose={mockOnClose} mode="edit" projectId={projectId} />, {
            wrapper: createWrapper(),
        })

        expect(await screen.findByText(/просмотр и редактирование/i)).toBeInTheDocument()

        const user = userEvent.setup()
        const nameInput = (await screen.findByLabelText(/название/i)) as HTMLInputElement
        await user.clear(nameInput)
        await user.type(nameInput, 'Project Updated')

        await user.click(screen.getByRole('button', { name: /сохранить/i }))

        await waitFor(() => {
            expect(mockProjectsApi.update).toHaveBeenCalledWith(projectId, {
                name: 'Project Updated',
                description: 'Desc',
            })
        })
    })

    it('allows editor to switch from view to edit and save', async () => {
        const mockAuthApi = vi.mocked(authApi)
        mockAuthApi.me.mockResolvedValue({
            id: 'editor-1',
            username: 'editor',
            email: 'editor@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })

        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.get.mockResolvedValue(mockProject as any)
        mockProjectsApi.listMembers.mockResolvedValue({
            members: [{ project_id: projectId, user_id: 'editor-1', role: 'editor', created_at: '2024-01-01T00:00:00Z' }],
        } as any)
        mockProjectsApi.update.mockResolvedValue({
            ...mockProject,
            description: 'New desc',
        } as any)

        render(<ProjectModal isOpen={true} onClose={mockOnClose} mode="view" projectId={projectId} />, {
            wrapper: createWrapper(),
        })

        // В режиме view появляется кнопка "Редактировать" для editor/owner
        const user = userEvent.setup()
        await user.click(await screen.findByRole('button', { name: /редактировать/i }))

        const desc = (await screen.findByLabelText(/описание/i)) as HTMLTextAreaElement
        await user.clear(desc)
        await user.type(desc, 'New desc')

        await user.click(screen.getByRole('button', { name: /сохранить/i }))

        await waitFor(() => {
            expect(mockProjectsApi.update).toHaveBeenCalledWith(projectId, {
                name: 'Project A',
                description: 'New desc',
            })
        })
    })
})

