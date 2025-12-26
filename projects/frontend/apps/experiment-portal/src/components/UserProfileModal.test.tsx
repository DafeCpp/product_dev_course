import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import UserProfileModal from './UserProfileModal'
import { projectsApi } from '../api/client'
import { authApi } from '../api/auth'
import { User, Project } from '../types'

// Mock API clients
vi.mock('../api/client', () => ({
    projectsApi: {
        list: vi.fn(),
        listMembers: vi.fn(),
    },
}))

vi.mock('../api/auth', () => ({
    authApi: {
        me: vi.fn(),
    },
}))

// Helper to create a QueryClient wrapper
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

describe('UserProfileModal', () => {
    const mockOnClose = vi.fn()

    const mockUser: User = {
        id: 'user-1',
        username: 'testuser',
        email: 'test@example.com',
        is_active: true,
        created_at: '2024-01-01T00:00:00Z',
    }

    const mockProjects: Project[] = [
        {
            id: 'project-1',
            name: 'Project 1',
            description: 'Description 1',
            owner_id: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        },
        {
            id: 'project-2',
            name: 'Project 2',
            description: null,
            owner_id: 'user-2',
            created_at: '2024-01-02T00:00:00Z',
            updated_at: '2024-01-02T00:00:00Z',
        },
    ]

    const mockMembers1 = {
        members: [
            {
                project_id: 'project-1',
                user_id: 'user-1',
                role: 'owner' as const,
                created_at: '2024-01-01T00:00:00Z',
                username: 'testuser',
            },
        ],
    }

    const mockMembers2 = {
        members: [
            {
                project_id: 'project-2',
                user_id: 'user-1',
                role: 'editor' as const,
                created_at: '2024-01-02T00:00:00Z',
                username: 'testuser',
            },
        ],
    }

    beforeEach(() => {
        vi.clearAllMocks()
        mockOnClose.mockClear()
    })

    it('renders modal when isOpen is true', async () => {
        vi.mocked(authApi.me).mockResolvedValue(mockUser)
        vi.mocked(projectsApi.list).mockResolvedValue({ projects: mockProjects })
        vi.mocked(projectsApi.listMembers)
            .mockResolvedValueOnce(mockMembers1)
            .mockResolvedValueOnce(mockMembers2)

        render(
            <UserProfileModal isOpen={true} onClose={mockOnClose} />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByRole('heading', { name: /профиль пользователя/i })).toBeInTheDocument()
        })
    })

    it('displays user information correctly', async () => {
        vi.mocked(authApi.me).mockResolvedValue(mockUser)
        vi.mocked(projectsApi.list).mockResolvedValue({ projects: mockProjects })
        vi.mocked(projectsApi.listMembers)
            .mockResolvedValueOnce(mockMembers1)
            .mockResolvedValueOnce(mockMembers2)

        render(
            <UserProfileModal isOpen={true} onClose={mockOnClose} />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('testuser')).toBeInTheDocument()
            expect(screen.getByText('test@example.com')).toBeInTheDocument()
            expect(screen.getByText('user-1')).toBeInTheDocument()
        })
    })

    it('displays projects list with roles', async () => {
        vi.mocked(authApi.me).mockResolvedValue(mockUser)
        vi.mocked(projectsApi.list).mockResolvedValue({ projects: mockProjects })
        vi.mocked(projectsApi.listMembers)
            .mockResolvedValueOnce(mockMembers1)
            .mockResolvedValueOnce(mockMembers2)

        render(
            <UserProfileModal isOpen={true} onClose={mockOnClose} />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('Project 1')).toBeInTheDocument()
            expect(screen.getByText('Project 2')).toBeInTheDocument()
        })

        // Check roles
        await waitFor(() => {
            expect(screen.getByText('Владелец')).toBeInTheDocument()
            expect(screen.getByText('Редактор')).toBeInTheDocument()
        })
    })

    it('shows empty state when user has no projects', async () => {
        vi.mocked(authApi.me).mockResolvedValue(mockUser)
        vi.mocked(projectsApi.list).mockResolvedValue({ projects: [] })

        render(
            <UserProfileModal isOpen={true} onClose={mockOnClose} />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText(/у вас нет доступных проектов/i)).toBeInTheDocument()
        })
    })

    it('shows loading state', () => {
        vi.mocked(authApi.me).mockImplementation(() => new Promise(() => {})) // Never resolves
        vi.mocked(projectsApi.list).mockImplementation(() => new Promise(() => {}))

        render(
            <UserProfileModal isOpen={true} onClose={mockOnClose} />,
            { wrapper: createWrapper() }
        )

        expect(screen.getByText(/загрузка/i)).toBeInTheDocument()
    })

    it('shows error state when user fetch fails', async () => {
        vi.mocked(authApi.me).mockRejectedValue(new Error('Failed to fetch user'))
        vi.mocked(projectsApi.list).mockResolvedValue({ projects: [] })

        render(
            <UserProfileModal isOpen={true} onClose={mockOnClose} />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('Failed to fetch user')).toBeInTheDocument()
        })
    })

    it('shows error state when projects fetch fails', async () => {
        vi.mocked(authApi.me).mockResolvedValue(mockUser)
        vi.mocked(projectsApi.list).mockRejectedValue(new Error('Failed to fetch projects'))

        render(
            <UserProfileModal isOpen={true} onClose={mockOnClose} />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('Failed to fetch projects')).toBeInTheDocument()
        })
    })

    it('does not render when isOpen is false', () => {
        render(
            <UserProfileModal isOpen={false} onClose={mockOnClose} />,
            { wrapper: createWrapper() }
        )

        expect(screen.queryByRole('heading', { name: /профиль пользователя/i })).not.toBeInTheDocument()
    })

    it('calls onClose when close button is clicked', async () => {
        vi.mocked(authApi.me).mockResolvedValue(mockUser)
        vi.mocked(projectsApi.list).mockResolvedValue({ projects: [] })

        render(
            <UserProfileModal isOpen={true} onClose={mockOnClose} />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('testuser')).toBeInTheDocument()
        })

        // Кнопка "Закрыть" находится в modal-actions
        const closeButton = screen.getByRole('button', { name: /закрыть/i })
        closeButton.click()

        await waitFor(() => {
            expect(mockOnClose).toHaveBeenCalledTimes(1)
        })
    })

    it('displays project description or placeholder', async () => {
        vi.mocked(authApi.me).mockResolvedValue(mockUser)
        vi.mocked(projectsApi.list).mockResolvedValue({ projects: mockProjects })
        vi.mocked(projectsApi.listMembers)
            .mockResolvedValueOnce(mockMembers1)
            .mockResolvedValueOnce(mockMembers2)

        render(
            <UserProfileModal isOpen={true} onClose={mockOnClose} />,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('Description 1')).toBeInTheDocument()
            expect(screen.getByText(/нет описания/i)).toBeInTheDocument()
        })
    })
})

