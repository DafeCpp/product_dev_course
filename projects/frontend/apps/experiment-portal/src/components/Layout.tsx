import { ReactNode, useEffect, useMemo, useState } from 'react'
import { Link, useLocation, useNavigate } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { authApi } from '../api/auth'
import UserProfileModal from './UserProfileModal'
import { notifyError, notifySuccess } from '../utils/notify'
import './Layout.scss'

interface LayoutProps {
  children: ReactNode
}

type NavItem = {
  to: string
  label: string
  description: string
  eyebrow: string
  shortLabel: string
  adminOnly?: boolean
}

const navItems: NavItem[] = [
  {
    to: '/projects',
    label: 'Проекты',
    description: 'Команды, доступ и рабочие пространства',
    eyebrow: 'Portfolio Layer',
    shortLabel: 'PR',
  },
  {
    to: '/experiments',
    label: 'Эксперименты',
    description: 'Гипотезы, статусы и оперативный контроль',
    eyebrow: 'Experiment Ops',
    shortLabel: 'EX',
  },
  {
    to: '/sensors',
    label: 'Датчики',
    description: 'Инвентарь, профили и регистрация устройств',
    eyebrow: 'Sensor Fleet',
    shortLabel: 'SN',
  },
  {
    to: '/sensor-monitor',
    label: 'Монитор датчиков',
    description: 'Статус подключения и история heartbeat',
    eyebrow: 'Sensor Monitor',
    shortLabel: 'SM',
  },
  {
    to: '/telemetry',
    label: 'Телеметрия',
    description: 'Потоки сигналов и живая диагностика',
    eyebrow: 'Signal Stream',
    shortLabel: 'TM',
  },
  {
    to: '/webhooks',
    label: 'Webhooks',
    description: 'Интеграции и внешние события',
    eyebrow: 'Automation',
    shortLabel: 'WH',
  },
  {
    to: '/admin/users',
    label: 'Администрирование',
    description: 'Роли, пользователи и контроль доступа',
    eyebrow: 'Control Plane',
    shortLabel: 'AD',
    adminOnly: true,
  },
]

const pageMeta = [
  {
    match: (pathname: string) => pathname.startsWith('/projects'),
    title: 'Проекты',
    description: 'Структурируйте команды, владельцев и зоны ответственности по портфелю.',
    eyebrow: 'Portfolio Layer',
  },
  {
    match: (pathname: string) => pathname.startsWith('/experiments'),
    title: 'Эксперименты',
    description: 'Следите за ходом исследований, статусами и контекстом каждого запуска.',
    eyebrow: 'Experiment Ops',
  },
  {
    match: (pathname: string) => pathname.startsWith('/runs'),
    title: 'Запуски',
    description: 'Развернутый обзор метрик, телеметрии и аудита по конкретному запуску.',
    eyebrow: 'Run Detail',
  },
  {
    match: (pathname: string) => pathname.startsWith('/sensors'),
    title: 'Датчики',
    description: 'Держите под контролем флот устройств, состояние каналов и токены доступа.',
    eyebrow: 'Sensor Fleet',
  },
  {
    match: (pathname: string) => pathname.startsWith('/sensor-monitor'),
    title: 'Монитор датчиков',
    description: 'Оперативный контроль статуса подключения и истории heartbeat по флоту.',
    eyebrow: 'Sensor Monitor',
  },
  {
    match: (pathname: string) => pathname.startsWith('/telemetry'),
    title: 'Телеметрия',
    description: 'Наблюдайте потоковые данные, историю и аномалии в одном окне.',
    eyebrow: 'Signal Stream',
  },
  {
    match: (pathname: string) => pathname.startsWith('/webhooks'),
    title: 'Webhooks',
    description: 'Настройка событий, доставок и внешней автоматизации для лабораторных процессов.',
    eyebrow: 'Automation',
  },
  {
    match: (pathname: string) => pathname.startsWith('/admin'),
    title: 'Администрирование',
    description: 'Управление доступом, пользователями и системными ролями.',
    eyebrow: 'Control Plane',
  },
]

function Layout({ children }: LayoutProps) {
  const sidebarStorageKey = 'experiment_portal_sidebar_desktop_collapsed'
  const compactViewportMaxWidth = 820
  const location = useLocation()
  const navigate = useNavigate()
  const queryClient = useQueryClient()
  const [isMenuOpen, setIsMenuOpen] = useState(false)
  const [isSidebarCollapsed, setIsSidebarCollapsed] = useState(false)
  const [isCompactViewport, setIsCompactViewport] = useState(false)
  const [isSidebarHovered, setIsSidebarHovered] = useState(false)
  const [isProfileModalOpen, setIsProfileModalOpen] = useState(false)

  const { data: user } = useQuery({
    queryKey: ['auth', 'me'],
    queryFn: () => authApi.me(),
    staleTime: 5 * 60 * 1000,
  })

  const logoutMutation = useMutation({
    mutationFn: () => authApi.logout(),
    onSuccess: () => {
      queryClient.clear()
      notifySuccess('Выход выполнен')
      navigate('/login')
    },
    onError: (err: any) => {
      const msg =
        err?.response?.data?.error ||
        err?.response?.data?.message ||
        err?.message ||
        'Ошибка выхода'
      notifyError(msg)
    },
  })

  const isAdmin = user?.is_admin || user?.system_roles?.some((r) => r === 'superadmin' || r === 'admin')
  const availableNavItems = useMemo(
    () => navItems.filter((item) => !item.adminOnly || isAdmin),
    [isAdmin]
  )

  const currentPage =
    pageMeta.find(({ match }) => match(location.pathname)) || {
      title: 'Experiment Portal',
      description: 'Операционная панель для экспериментов, устройств и данных.',
      eyebrow: 'Operations',
    }

  const handleLogout = () => {
    logoutMutation.mutate()
  }

  const shouldDefaultCollapseDesktop = () =>
    typeof window !== 'undefined' && window.innerWidth <= 1440

  useEffect(() => {
    if (typeof window === 'undefined') return

    const syncViewport = () => {
      const compact = window.innerWidth <= compactViewportMaxWidth
      setIsCompactViewport(compact)
      if (compact) {
        setIsSidebarHovered(false)
      }
      if (!compact) {
        setIsMenuOpen(false)
      }
    }

    syncViewport()
    window.addEventListener('resize', syncViewport)

    const stored = window.localStorage.getItem(sidebarStorageKey)

    if (stored === '1' || stored === '0') {
      setIsSidebarCollapsed(stored === '1')
    } else {
      setIsSidebarCollapsed(shouldDefaultCollapseDesktop())
    }

    return () => {
      window.removeEventListener('resize', syncViewport)
    }
  }, [])

  useEffect(() => {
    if (typeof window === 'undefined') return
    window.localStorage.setItem(sidebarStorageKey, isSidebarCollapsed ? '1' : '0')
  }, [isSidebarCollapsed])

  const toggleSidebar = () => {
    if (isCompactViewport) {
      setIsMenuOpen((prev) => !prev)
      return
    }

    setIsSidebarHovered(false)
    setIsSidebarCollapsed((prev) => !prev)
  }

  const isSidebarHidden = isCompactViewport ? !isMenuOpen : isSidebarCollapsed
  const isSidebarHoverExpanded = !isCompactViewport && isSidebarCollapsed && isSidebarHovered
  const desktopSidebarWidth = isSidebarCollapsed && !isSidebarHoverExpanded ? 84 : 288
  const layoutStyle = isCompactViewport
    ? undefined
    : {
        gridTemplateColumns: `${desktopSidebarWidth}px minmax(0, 1fr)`,
      }
  const sidebarStyle = isCompactViewport
    ? undefined
    : {
        width: `${desktopSidebarWidth}px`,
        minWidth: `${desktopSidebarWidth}px`,
      }

  return (
    <div
      className={`layout${isMenuOpen ? ' menu-open' : ''}${isSidebarCollapsed ? ' sidebar-collapsed' : ''}${
        isCompactViewport ? ' compact-viewport' : ''
      }${isSidebarHoverExpanded ? ' sidebar-hover-expanded' : ''}`}
      style={layoutStyle}
    >
      <button
        type="button"
        className={`layout-backdrop${isMenuOpen ? ' visible' : ''}`}
        onClick={() => setIsMenuOpen(false)}
        aria-label="Закрыть навигацию"
      />

      <aside
        className="sidebar"
        style={sidebarStyle}
        onMouseEnter={() => {
          if (!isCompactViewport && isSidebarCollapsed) {
            setIsSidebarHovered(true)
          }
        }}
        onMouseLeave={() => {
          if (!isCompactViewport) {
            setIsSidebarHovered(false)
          }
        }}
      >
        <div className="sidebar__inner">
          <Link
            to="/experiments"
            className="sidebar-brand"
            onClick={() => setIsMenuOpen(false)}
          >
            <span className="sidebar-brand__eyebrow">Bartini Labs</span>
            <span className="sidebar-brand__mark">LA</span>
            <span className="sidebar-brand__title">Experiment Portal</span>
            <span className="sidebar-brand__caption">
              Командный центр для проектов, сенсоров, запусков и потоковой телеметрии.
            </span>
          </Link>

          <nav className="nav" aria-label="Основная навигация">
            {availableNavItems.map((item, index) => (
              <Link
                key={item.to}
                to={item.to}
                className={`nav__item${location.pathname.startsWith(item.to) ? ' active' : ''}`}
                onClick={() => setIsMenuOpen(false)}
              >
                <span className="nav__item-icon" aria-hidden="true">
                  <span className="nav__item-index">{String(index + 1).padStart(2, '0')}</span>
                  <span className="nav__item-short">{item.shortLabel}</span>
                </span>
                <span className="nav__item-copy">
                  <span className="nav__item-label">{item.label}</span>
                  <span className="nav__item-description">{item.description}</span>
                </span>
              </Link>
            ))}
          </nav>
        </div>
      </aside>

      <div className="layout-shell">
        <header className="header">
          <div className="header-content">
            <div className="header-copy">
              <span className="header-copy__eyebrow">{currentPage.eyebrow}</span>
              <div>
                <h1>{currentPage.title}</h1>
                <p>{currentPage.description}</p>
              </div>
            </div>

            <div className="header-actions">
              <button
                type="button"
                className="layout-desktop-nav-toggle btn btn-secondary btn-sm"
                onClick={toggleSidebar}
                aria-label={isSidebarHidden ? 'Показать меню' : 'Скрыть меню'}
                aria-pressed={!isSidebarHidden}
              >
                <span className="layout-nav-toggle__icon" aria-hidden="true">
                  <span />
                  <span />
                  <span />
                </span>
                <span className="layout-nav-toggle__label">
                  {isSidebarHidden ? 'Показать меню' : 'Скрыть меню'}
                </span>
              </button>
              <div className="header-chip">
                <span className="header-chip__label">Workspace</span>
                <span className="header-chip__value">
                  {isAdmin ? 'Admin access' : 'Member access'}
                </span>
              </div>

              {user && (
                <div className="user-info">
                  <button
                    className="username-link"
                    onClick={() => setIsProfileModalOpen(true)}
                    title="Открыть профиль"
                  >
                    <span className="username-link__name">{user.username}</span>
                    <span className="username-link__meta">{user.email || 'Профиль пользователя'}</span>
                  </button>
                  <button
                    className="btn btn-secondary btn-sm"
                    onClick={handleLogout}
                    disabled={logoutMutation.isPending}
                  >
                    {logoutMutation.isPending ? 'Выход...' : 'Выйти'}
                  </button>
                </div>
              )}
            </div>
          </div>
        </header>

        <main className="main">
          <div className="container">
            <div key={location.pathname} className="page-transition">
              {children}
            </div>
          </div>
        </main>
      </div>

      <UserProfileModal
        isOpen={isProfileModalOpen}
        onClose={() => setIsProfileModalOpen(false)}
      />
    </div>
  )
}

export default Layout
