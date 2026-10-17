import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { projectsApi } from '../api/client'
import { authApi } from '../api/auth'
import { createMockProjectMember } from '../testUtils/factories'

// В тестах не нужен настоящий portal (иногда дает шум в stderr),
// поэтому упрощаем: createPortal возвращает element как есть.
vi.mock('react-dom', async () => {
    const actual = await vi.importActual<any>('react-dom')
    return { ...actual, createPortal: (node: any) => node }
})

vi.mock('../api/client', () => ({
    projectsApi: {
        list: vi.fn(),
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

describe('ProjectsList', () => {
    const ownerProject = {
        id: 'project-owner',
        name: 'Owner Project',
        description: 'Owned',
        owner_id: 'user-1',
        created_at: '2024-01-01T00:00:00Z',
        updated_at: '2024-01-01T00:00:00Z',
    }

    const otherProject = {
        id: 'project-view',
        name: 'View Project',
        description: 'View only',
        owner_id: 'someone-else',
        created_at: '2024-01-01T00:00:00Z',
        updated_at: '2024-01-01T00:00:00Z',
    }

    beforeEach(() => {
        vi.clearAllMocks()
        vi.mocked(projectsApi).list.mockResolvedValue({
            projects: [ownerProject as any, otherProject as any],
        } as any)
        vi.mocked(authApi).me.mockResolvedValue({
            id: 'user-1',
            username: 'user1',
            email: 'u1@example.com',
            is_active: true,
            is_admin: false,
            system_roles: [],
            password_change_required: false,
            created_at: '2024-01-01T00:00:00Z',
        })
    })

    it('opens ProjectModal in create mode', async () => {
        const { default: ProjectsList } = await import('./ProjectsList')
        render(<ProjectsList />, { wrapper: createWrapper() })

        const user = userEvent.setup()
        await user.click(await screen.findByRole('button', { name: /создать проект/i }))

        expect(await screen.findByRole('heading', { name: /создать проект/i })).toBeInTheDocument()
    })

    it('opens ProjectModal in edit mode for owner project', async () => {
        const { default: ProjectsList } = await import('./ProjectsList')
        vi.mocked(projectsApi).get.mockResolvedValue(ownerProject as any)
        vi.mocked(projectsApi).listMembers.mockResolvedValue({ members: [] } as any)

        render(<ProjectsList />, { wrapper: createWrapper() })

        const user = userEvent.setup()
        // Нажимаем кнопку ✏️ на карточке владельца (aria-label="Открыть проект")
        const openButtons = await screen.findAllByRole('button', { name: 'Открыть проект' })
        await user.click(openButtons[0])

        expect(await screen.findByRole('heading', { name: /просмотр и редактирование/i })).toBeInTheDocument()
    })

    it('opens ProjectModal in view mode for non-owner project', async () => {
        const { default: ProjectsList } = await import('./ProjectsList')
        vi.mocked(projectsApi).get.mockResolvedValue(otherProject as any)
        vi.mocked(projectsApi).listMembers.mockResolvedValue({
            members: [createMockProjectMember({ project_id: otherProject.id, user_id: 'user-1', roles: ['viewer'] })],
        })

        render(<ProjectsList />, { wrapper: createWrapper() })

        const user = userEvent.setup()
        const openButtons = await screen.findAllByRole('button', { name: 'Открыть проект' })
        await user.click(openButtons[1])

        expect(await screen.findByRole('heading', { name: /проект: просмотр/i })).toBeInTheDocument()

        // В режиме просмотра кнопки "Сохранить" быть не должно
        await waitFor(() => {
            expect(screen.queryByRole('button', { name: /сохранить/i })).not.toBeInTheDocument()
        })
    })
})

