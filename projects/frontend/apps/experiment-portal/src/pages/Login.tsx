import { useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { useMutation, useQueryClient } from '@tanstack/react-query'
import { authApi } from '../api/auth'
import type { LoginRequest } from '../types'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'
import './Login.scss'

function Login() {
  const navigate = useNavigate()
  const queryClient = useQueryClient()
  const [formData, setFormData] = useState<LoginRequest>({
    username: '',
    password: '',
  })
  const [error, setError] = useState<string | null>(null)

  const loginMutation = useMutation({
    mutationFn: (credentials: LoginRequest) => authApi.login(credentials),
    onSuccess: async (data) => {
      await queryClient.invalidateQueries({ queryKey: ['auth', 'me'] })
      notifySuccess('Вход выполнен')
      if (data?.user?.password_change_required) {
        navigate('/change-password', { replace: true })
      } else {
        navigate('/experiments')
      }
    },
    onError: (err: any) => {
      const msg =
        err.response?.data?.error ||
        err.response?.data?.message ||
        'Ошибка входа. Проверьте имя пользователя и пароль.'
      setError(msg)
      notifyError(msg)
    },
  })

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault()
    setError(null)

    if (!formData.username.trim()) {
      const msg = 'Введите имя пользователя'
      setError(msg)
      notifyError(msg)
      return
    }

    if (!formData.password) {
      const msg = 'Введите пароль'
      setError(msg)
      notifyError(msg)
      return
    }

    loginMutation.mutate({
      username: formData.username.trim(),
      password: formData.password,
    })
  }

  return (
    <div className="auth-page auth-page--login">
      <div className="auth-shell">
        <section className="auth-showcase">
          <span className="auth-showcase__eyebrow">Experiment Ops</span>
          <h1>Experiment Portal</h1>
          <p>
            Соберите эксперименты, сенсоры, телеметрию и проектную структуру в одном контуре
            управления без визуального шума и лишних переходов.
          </p>

          <div className="auth-showcase__grid">
            <div className="auth-showcase__metric">
              <strong>Projects</strong>
              <span>Рабочие пространства, владельцы и роли доступа.</span>
            </div>
            <div className="auth-showcase__metric">
              <strong>Runs</strong>
              <span>Статусы, запусковые данные и метрики по экспериментам.</span>
            </div>
            <div className="auth-showcase__metric">
              <strong>Telemetry</strong>
              <span>Потоки сигналов, heartbeat и диагностика сенсоров.</span>
            </div>
          </div>
        </section>

        <div className="login-card card auth-card">
          <div className="auth-card__intro">
            <span className="auth-card__eyebrow">Secure entry</span>
            <h2 className="login-title">Вход в систему</h2>
            <p className="login-subtitle">
              Продолжайте работу с экспериментами, командами и телеметрией.
            </p>
          </div>

          {IS_TEST && error && <div className="error-message">{error}</div>}

          <form onSubmit={handleSubmit} className="login-form auth-form">
            <div className="form-group">
              <label htmlFor="username">Имя пользователя</label>
              <input
                id="username"
                type="text"
                value={formData.username}
                onChange={(e) => setFormData({ ...formData, username: e.target.value })}
                required
                autoComplete="username"
                placeholder="Введите имя пользователя"
                disabled={loginMutation.isPending}
              />
            </div>

            <div className="form-group">
              <label htmlFor="password">Пароль</label>
              <input
                id="password"
                type="password"
                value={formData.password}
                onChange={(e) => setFormData({ ...formData, password: e.target.value })}
                required
                autoComplete="current-password"
                placeholder="Введите пароль"
                disabled={loginMutation.isPending}
              />
            </div>

            <button
              type="submit"
              className="btn btn-primary btn-block"
              disabled={loginMutation.isPending}
            >
              {loginMutation.isPending ? 'Вход...' : 'Войти'}
            </button>
          </form>

          <p className="auth-note">
            Доступ открывает весь рабочий контур: проекты, эксперименты, устройства и историю
            запусков.
          </p>

          <div className="auth-switch">
            <Link to="/forgot-password">Забыли пароль?</Link>
          </div>

          <div className="auth-switch">
            Нет аккаунта? <Link to="/register">Зарегистрироваться</Link>
          </div>
        </div>
      </div>
    </div>
  )
}

export default Login
