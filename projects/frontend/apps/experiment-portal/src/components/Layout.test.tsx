import { describe, expect, it, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import Layout from './Layout'
import { authApi } from '../api/auth'

// Мокаем authApi
vi.mock('../api/auth', () => ({
    authApi: {
        me: vi.fn(),
        logout: vi.fn(),
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
        <QueryClientProvider client={queryClient}>
            <MemoryRouter initialEntries={['/experiments']}>
                {children}
            </MemoryRouter>
        </QueryClientProvider>
    )
}

describe('Layout', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        window.localStorage.clear()
        const mockMe = vi.mocked(authApi.me)
        mockMe.mockResolvedValue({
            id: '1',
            username: 'testuser',
            email: 'test@example.com',
            is_active: true,
            created_at: '2024-01-01T00:00:00Z',
        })
    })

    it('renders header links and page content', async () => {
        render(
            <Layout>
                <div>Test content</div>
            </Layout>,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(
                screen.getByRole('heading', {
                    name: /эксперименты/i,
                })
            ).toBeInTheDocument()
        })

        expect(screen.getByText('Test content')).toBeInTheDocument()
        // Ищем ссылку на страницу экспериментов по href (в свернутом меню у ссылок нет текстовых labels)
        const allLinks = screen.getAllByRole('link')
        // Навигационная ссылка имеет класс nav__item (в отличие от логотипа)
        const navLink = allLinks.find((el) => 
            el.getAttribute('href') === '/experiments' && 
            el.classList.contains('nav__item')
        )
        expect(navLink).toBeInTheDocument()
        expect(navLink).toHaveClass('active')
    })

    it('displays username when user is authenticated', async () => {
        render(
            <Layout>
                <div>Test content</div>
            </Layout>,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByText('testuser')).toBeInTheDocument()
        })
    })

    it('renders logout button', async () => {
        render(
            <Layout>
                <div>Test content</div>
            </Layout>,
            { wrapper: createWrapper() }
        )

        await waitFor(() => {
            expect(screen.getByRole('button', { name: /выйти/i })).toBeInTheDocument()
        })
    })

    it('clears workspace storage after a successful logout', async () => {
        const user = userEvent.setup()
        vi.mocked(authApi.logout).mockResolvedValue(undefined)
        window.localStorage.setItem('experiment_portal.active_project_id', 'project-1')
        window.localStorage.setItem('telemetry_panel_ids', '["panel-1"]')
        window.localStorage.setItem('telemetry_history_state', '{"sensorIds":["sensor-1"]}')
        window.localStorage.setItem('telemetry_viewer_state', '{"projectId":"project-1"}')
        window.localStorage.setItem('telemetry_panel_state_panel-1', '{"selectedSensorIds":["sensor-1"]}')
        window.localStorage.setItem('experiment_portal_sidebar_desktop_collapsed', '1')

        render(
            <Layout>
                <div>Test content</div>
            </Layout>,
            { wrapper: createWrapper() }
        )

        await user.click(await screen.findByRole('button', { name: /выйти/i }))

        await waitFor(() => {
            expect(authApi.logout).toHaveBeenCalledOnce()
            expect(window.localStorage.getItem('experiment_portal.active_project_id')).toBeNull()
        })
        expect(window.localStorage.getItem('telemetry_panel_ids')).toBeNull()
        expect(window.localStorage.getItem('telemetry_history_state')).toBeNull()
        expect(window.localStorage.getItem('telemetry_viewer_state')).toBeNull()
        expect(window.localStorage.getItem('telemetry_panel_state_panel-1')).toBeNull()
        expect(window.localStorage.getItem('experiment_portal_sidebar_desktop_collapsed')).toBe('1')
    })
})
