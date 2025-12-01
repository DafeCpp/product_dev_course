import { ReactNode } from 'react'
import { Link, useLocation } from 'react-router-dom'
import './Layout.css'

interface LayoutProps {
  children: ReactNode
}

function Layout({ children }: LayoutProps) {
  const location = useLocation()

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
            </nav>
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

