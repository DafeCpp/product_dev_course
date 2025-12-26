import { ReactNode } from 'react'
import { Link, useLocation, useNavigate } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { authApi } from '../api/auth'
import './Layout.css'

interface LayoutProps {
  children: ReactNode
}

function Layout({ children }: LayoutProps) {
  const location = useLocation()
  const navigate = useNavigate()
  const queryClient = useQueryClient()

  const { data: user } = useQuery({
    queryKey: ['auth', 'me'],
    queryFn: () => authApi.me(),
    staleTime: 5 * 60 * 1000, // 5 минут
  })

  const logoutMutation = useMutation({
    mutationFn: () => authApi.logout(),
    onSuccess: () => {
      queryClient.clear()
      navigate('/login')
    },
  })

  const handleLogout = () => {
    logoutMutation.mutate()
  }

  return (
    <div className="layout">
      <header className="header">
        <div className="container">
          <div className="header-content">
            <Link to="/" className="logo">
              <h1>Experiment Tracking</h1>
            </Link>
            <nav className="nav">
              <Link
                to="/experiments"
                className={location.pathname.startsWith('/experiments') ? 'active' : ''}
              >
                Эксперименты
              </Link>
              <Link
                to="/experiments/new"
                className={location.pathname === '/experiments/new' ? 'active' : ''}
              >
                Новый эксперимент
              </Link>
              <Link
                to="/sensors"
                className={location.pathname.startsWith('/sensors') ? 'active' : ''}
              >
                Датчики
              </Link>
            </nav>
            <div className="user-menu">
              {user && (
                <div className="user-info">
                  <span className="username">{user.username}</span>
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
    </div>
  )
}

export default Layout

