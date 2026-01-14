import { ReactNode, useState } from 'react'
import { Link, useLocation, useNavigate } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { authApi } from '../api/auth'
import UserProfileModal from './UserProfileModal'
import { notifyError, notifySuccess } from '../utils/notify'
import './Layout.css'

interface LayoutProps {
  children: ReactNode
}

function Layout({ children }: LayoutProps) {
  const location = useLocation()
  const navigate = useNavigate()
  const queryClient = useQueryClient()
  const [isMenuOpen, setIsMenuOpen] = useState(false)
  const [isProfileModalOpen, setIsProfileModalOpen] = useState(false)

  const { data: user } = useQuery({
    queryKey: ['auth', 'me'],
    queryFn: () => authApi.me(),
    staleTime: 5 * 60 * 1000, // 5 минут
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

  const handleLogout = () => {
    logoutMutation.mutate()
  }

  return (
    <div className="layout">
      <div
        className="sidebar-trigger"
        onMouseEnter={() => setIsMenuOpen(true)}
        onMouseLeave={() => setIsMenuOpen(false)}
      >
        <div className={`sidebar ${isMenuOpen ? 'open' : ''}`}>
          <nav className="nav">
            <Link
              to="/projects"
              className={location.pathname.startsWith('/projects') ? 'active' : ''}
            >
              Проекты
            </Link>
            <Link
              to="/experiments"
              className={location.pathname.startsWith('/experiments') ? 'active' : ''}
            >
              Эксперименты
            </Link>
            <Link
              to="/sensors"
              className={location.pathname.startsWith('/sensors') ? 'active' : ''}
            >
              Датчики
            </Link>
            <Link
              to="/telemetry"
              className={location.pathname.startsWith('/telemetry') ? 'active' : ''}
            >
              Телеметрия
            </Link>
          </nav>
        </div>
      </div>
      <header className="header">
        <div className="header-container">
          <div className="header-content">
            <Link to="/" className="logo">
              <h1>Experiment Tracking</h1>
            </Link>
            <div className="user-menu">
              {user && (
                <div className="user-info">
                  <button
                    className="username-link"
                    onClick={() => setIsProfileModalOpen(true)}
                    title="Открыть профиль"
                  >
                    {user.username}
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
        </div>
      </header>
      <main className="main">
        <div className="container">{children}</div>
      </main>

      <UserProfileModal
        isOpen={isProfileModalOpen}
        onClose={() => setIsProfileModalOpen(false)}
      />
    </div>
  )
}

export default Layout

