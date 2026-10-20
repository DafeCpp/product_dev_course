import { describe, expect, it, vi, beforeEach, afterEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { act, fireEvent, render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import Layout from './Layout'
import { authApi } from '../api/auth'
import { usePermissions } from '../hooks/usePermissions'

vi.mock('../api/auth', () => ({
    authApi: { me: vi.fn(), logout: vi.fn() },
}))

vi.mock('../hooks/usePermissions', () => ({
    usePermissions: vi.fn(),
}))

vi.mock('./UserProfileModal', () => ({
    default: ({ isOpen, onClose }: { isOpen: boolean; onClose: () => void }) =>
        isOpen ? (
            <div data-testid="profile-modal">
                <button onClick={onClose}>close-profile</button>
            </div>
        ) : null,
}))

const STORAGE_KEY = 'experiment_portal_sidebar_desktop_collapsed'

const baseUser = {
    id: '1',
    username: 'tester',
    email: 'tester@example.com',
    is_active: true,
    is_admin: false,
    system_roles: [] as string[],
    password_change_required: false,
    created_at: '2024-01-01T00:00:00Z',
}

const setPermissions = (opts: { isSuperadmin?: boolean; perms?: string[] }) => {
    const perms = opts.perms ?? []
    vi.mocked(usePermissions).mockReturnValue({
        isSuperadmin: opts.isSuperadmin ?? false,
        hasAnyPermission: (...p: string[]) => p.some((x) => perms.includes(x)),
    } as unknown as ReturnType<typeof usePermissions>)
}

const renderLayout = (path = '/experiments') => {
    const qc = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } })
    return render(
        <QueryClientProvider client={qc}>
            <MemoryRouter initialEntries={[path]}>
                <Layout>
                    <div>content</div>
                </Layout>
            </MemoryRouter>
        </QueryClientProvider>
    )
}

const setWidth = (w: number) => {
    Object.defineProperty(window, 'innerWidth', { value: w, configurable: true, writable: true })
}

const navLabels = () =>
    Array.from(document.querySelectorAll('.nav__item-label')).map((el) => el.textContent)

describe('Layout branches', () => {
    const originalWidth = window.innerWidth

    beforeEach(() => {
        vi.clearAllMocks()
        window.localStorage.clear()
        setWidth(1024)
        setPermissions({})
        vi.mocked(authApi.me).mockResolvedValue(baseUser)
    })

    afterEach(() => {
        setWidth(originalWidth)
    })

    it.each([
        ['/projects/1', 'Проекты'],
        ['/runs/abc', 'Запуски'],
        ['/sensors', 'Датчики'],
        ['/sensor-monitor', 'Монитор датчиков'],
        ['/telemetry', 'Телеметрия'],
        ['/webhooks', 'Webhooks'],
        ['/admin/audit', 'Аудит'],
        ['/admin/scripts', 'Скрипты'],
        ['/admin/configs', 'Конфиги'],
        ['/admin/rate-limits', 'Rate Limits & QoS'],
        ['/admin/users', 'Администрирование'],
        ['/somewhere', 'Experiment Portal'],
    ])('renders page meta for %s', async (path, title) => {
        renderLayout(path)
        expect(screen.getByRole('heading', { level: 1, name: title })).toBeInTheDocument()
        await screen.findByText('tester')
    })

    it('hides permission-gated nav items for regular members', async () => {
        renderLayout()
        await screen.findByText('tester')
        expect(navLabels()).not.toContain('Пользователи')
        expect(navLabels()).not.toContain('Аудит')
        expect(screen.getByText('Member access')).toBeInTheDocument()
    })

    it('shows only items matching granted permissions', async () => {
        setPermissions({ perms: ['audit.read'] })
        renderLayout()
        await screen.findByText('tester')
        expect(navLabels()).toContain('Аудит')
        expect(navLabels()).not.toContain('Скрипты')
    })

    it('shows all items and admin access for superadmin', async () => {
        setPermissions({ isSuperadmin: true })
        renderLayout()
        await screen.findByText('tester')
        expect(navLabels()).toHaveLength(11)
        expect(screen.getByText('Admin access')).toBeInTheDocument()
    })

    it('shows admin access for admin system role and email fallback', async () => {
        vi.mocked(authApi.me).mockResolvedValue({ ...baseUser, email: '', system_roles: ['admin'] })
        renderLayout()
        expect(await screen.findByText('Профиль пользователя')).toBeInTheDocument()
        expect(screen.getByText('Admin access')).toBeInTheDocument()
    })

    it('treats missing system_roles as member and no user hides user info', async () => {
        vi.mocked(authApi.me).mockResolvedValue({ ...baseUser, system_roles: undefined } as never)
        renderLayout()
        await screen.findByText('tester')
        expect(screen.getByText('Member access')).toBeInTheDocument()
    })

    it('hides user block when me fails', async () => {
        vi.mocked(authApi.me).mockRejectedValue(new Error('401'))
        renderLayout()
        await act(async () => {})
        expect(screen.queryByRole('button', { name: 'Выйти' })).not.toBeInTheDocument()
    })

    it('shows pending logout label', async () => {
        const user = userEvent.setup()
        vi.mocked(authApi.logout).mockImplementation(() => new Promise(() => {}))
        renderLayout()
        await user.click(await screen.findByRole('button', { name: 'Выйти' }))
        expect(await screen.findByRole('button', { name: 'Выход...' })).toBeDisabled()
    })

    it('opens and closes the profile modal', async () => {
        const user = userEvent.setup()
        renderLayout()
        await user.click(await screen.findByTitle('Открыть профиль'))
        expect(screen.getByTestId('profile-modal')).toBeInTheDocument()
        await user.click(screen.getByRole('button', { name: 'close-profile' }))
        expect(screen.queryByTestId('profile-modal')).not.toBeInTheDocument()
    })

    it('desktop: defaults to collapsed on narrow desktop, hover expands, toggle persists', async () => {
        const user = userEvent.setup()
        const { container } = renderLayout()
        await screen.findByText('tester')
        const layout = container.querySelector('.layout') as HTMLElement
        const sidebar = container.querySelector('.sidebar') as HTMLElement
        expect(layout).toHaveClass('sidebar-collapsed')
        expect(layout).not.toHaveClass('compact-viewport')
        expect(sidebar.style.width).toBe('84px')
        expect(window.localStorage.getItem(STORAGE_KEY)).toBe('1')

        fireEvent.mouseEnter(sidebar)
        expect(layout).toHaveClass('sidebar-hover-expanded')
        expect(sidebar.style.width).toBe('288px')
        fireEvent.mouseLeave(sidebar)
        expect(layout).not.toHaveClass('sidebar-hover-expanded')

        await user.click(screen.getByRole('button', { name: 'Показать меню' }))
        expect(layout).not.toHaveClass('sidebar-collapsed')
        expect(window.localStorage.getItem(STORAGE_KEY)).toBe('0')
        expect(screen.getByRole('button', { name: 'Скрыть меню' })).toHaveAttribute('aria-pressed', 'true')

        fireEvent.mouseEnter(sidebar)
        expect(layout).not.toHaveClass('sidebar-hover-expanded')
    })

    it('desktop: uses stored "0" preference and wide default', async () => {
        window.localStorage.setItem(STORAGE_KEY, '0')
        const { container, unmount } = renderLayout()
        await screen.findByText('tester')
        expect(container.querySelector('.layout')).not.toHaveClass('sidebar-collapsed')
        unmount()

        window.localStorage.clear()
        setWidth(1600)
        const second = renderLayout()
        await screen.findByText('tester')
        expect(second.container.querySelector('.layout')).not.toHaveClass('sidebar-collapsed')
    })

    it('compact viewport: toggle opens menu, backdrop/nav/brand close, resize to desktop closes', async () => {
        const user = userEvent.setup()
        setWidth(500)
        window.localStorage.setItem(STORAGE_KEY, '1')
        const { container } = renderLayout()
        await screen.findByText('tester')
        const layout = container.querySelector('.layout') as HTMLElement
        const sidebar = container.querySelector('.sidebar') as HTMLElement
        expect(layout).toHaveClass('compact-viewport')
        expect(layout.style.gridTemplateColumns).toBe('')
        expect(sidebar.style.width).toBe('')

        fireEvent.mouseEnter(sidebar)
        fireEvent.mouseLeave(sidebar)
        expect(layout).not.toHaveClass('sidebar-hover-expanded')

        const toggle = screen.getByRole('button', { name: 'Показать меню' })
        await user.click(toggle)
        expect(layout).toHaveClass('menu-open')
        const backdrop = screen.getByLabelText('Закрыть навигацию')
        expect(backdrop).toHaveClass('visible')
        fireEvent.click(backdrop)
        expect(layout).not.toHaveClass('menu-open')

        await user.click(toggle)
        const navLink = container.querySelector('a.nav__item[href="/projects"]') as HTMLElement
        await user.click(navLink)
        expect(layout).not.toHaveClass('menu-open')
        expect(screen.getByRole('heading', { level: 1, name: 'Проекты' })).toBeInTheDocument()

        await user.click(screen.getByRole('button', { name: 'Показать меню' }))
        await user.click(container.querySelector('a.sidebar-brand') as HTMLElement)
        expect(layout).not.toHaveClass('menu-open')

        await user.click(screen.getByRole('button', { name: 'Показать меню' }))
        expect(layout).toHaveClass('menu-open')
        await user.click(screen.getByRole('button', { name: 'Скрыть меню' }))
        expect(layout).not.toHaveClass('menu-open')
        await user.click(screen.getByRole('button', { name: 'Показать меню' }))
        act(() => {
            setWidth(1200)
            window.dispatchEvent(new Event('resize'))
        })
        expect(layout).not.toHaveClass('menu-open')
        expect(layout).not.toHaveClass('compact-viewport')
    })
})
