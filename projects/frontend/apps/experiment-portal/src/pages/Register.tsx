import { isClientRequestError } from '../utils/apiError'
import { useState } from 'react'
import { Link, useNavigate, useSearchParams } from 'react-router-dom'
import { authApi } from '../api/auth'
import type { RegisterRequest } from '../types'
import { notifySuccess } from '../utils/notify'
import { useApiMutation } from '../hooks/useApiMutation'
import './Register.scss'

function Register() {
  const navigate = useNavigate()
  const [searchParams] = useSearchParams()
  const inviteToken = searchParams.get('token') ?? undefined
  const emailHint = searchParams.get('email_hint') ?? ''

  const [formData, setFormData] = useState<RegisterRequest & { confirmPassword: string }>({
    username: '',
    email: emailHint,
    password: '',
    confirmPassword: '',
  })
  const [error, setError] = useState<string | null>(null)

  const registerMutation = useApiMutation({
    mutationFn: (payload: RegisterRequest) => authApi.register(payload),
    onSuccess: () => {
      notifySuccess('Регистрация успешна. Войдите в систему.')
      navigate('/login')
    },
    errorFallback: (error) =>
      isClientRequestError(error)
        ? 'Ошибка регистрации. Проверьте введенные данные.'
        : 'Не удалось зарегистрироваться: сервис недоступен. Попробуйте позже.',
    inlineError: setError,
  })

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault()
    setError(null)

    const username = formData.username.trim()
    const email = formData.email.trim()

    if (!username) {
      setError('Введите имя пользователя')
      return
    }

    if (username.length < 3) {
      setError('Имя пользователя должно быть не короче 3 символов')
      return
    }

    if (!email) {
      setError('Введите email')
      return
    }

    if (!/^\S+@\S+\.\S+$/.test(email)) {
      setError('Введите корректный email')
      return
    }

    if (!formData.password) {
      setError('Введите пароль')
      return
    }

    if (formData.password.length < 8) {
      setError('Пароль должен быть не короче 8 символов')
      return
    }

    if (!formData.confirmPassword) {
      setError('Повторите пароль')
      return
    }

    if (formData.password !== formData.confirmPassword) {
      setError('Пароли не совпадают')
      return
    }

    registerMutation.mutate({
      username,
      email,
      password: formData.password,
      ...(inviteToken ? { invite_token: inviteToken } : {}),
    })
  }

  return (
    <div className="auth-page auth-page--register">
      <div className="auth-shell">
        <section className="auth-showcase">
          <span className="auth-showcase__eyebrow">Team Onboarding</span>
          <h1>Подключите команду к единому порталу экспериментов</h1>
          <p>
            Регистрация открывает общий контур работы: от проектной структуры и ролей доступа до
            диагностики сенсоров и журналов запусков.
          </p>

          <div className="auth-showcase__grid">
            <div className="auth-showcase__metric">
              <strong>Access</strong>
              <span>Роли и зоны ответственности для каждой проектной команды.</span>
            </div>
            <div className="auth-showcase__metric">
              <strong>Audit</strong>
              <span>Прозрачная история действий и изменений по каждому контексту.</span>
            </div>
            <div className="auth-showcase__metric">
              <strong>Signal</strong>
              <span>Доступ к телеметрии и живой диагностике без отдельного набора инструментов.</span>
            </div>
          </div>
        </section>

        <div className="register-card card auth-card">
          <div className="auth-card__intro">
            <span className="auth-card__eyebrow">Create account</span>
            <h2 className="register-title">Регистрация</h2>
            <p className="register-subtitle">
              Создайте учетную запись для доступа к проектам, экспериментам и данным.
            </p>
          </div>

          {inviteToken && (
            <div className="invite-banner">
              <span className="invite-banner__icon">✉</span>
              <span>
                Регистрация по приглашению
                {emailHint && <strong> для {emailHint}</strong>}
              </span>
            </div>
          )}

          {error && <div className="error-message" role="alert">{error}</div>}

          <form onSubmit={handleSubmit} className="register-form auth-form">
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
                disabled={registerMutation.isPending}
              />
            </div>

            <div className="form-group">
              <label htmlFor="email">Email</label>
              <input
                id="email"
                type="email"
                value={formData.email}
                onChange={(e) => setFormData({ ...formData, email: e.target.value })}
                required
                autoComplete="email"
                placeholder="Введите email"
                disabled={registerMutation.isPending}
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
                autoComplete="new-password"
                placeholder="Придумайте пароль"
                disabled={registerMutation.isPending}
              />
            </div>

            <div className="form-group">
              <label htmlFor="confirmPassword">Повторите пароль</label>
              <input
                id="confirmPassword"
                type="password"
                value={formData.confirmPassword}
                onChange={(e) =>
                  setFormData({ ...formData, confirmPassword: e.target.value })
                }
                required
                autoComplete="new-password"
                placeholder="Повторите пароль"
                disabled={registerMutation.isPending}
              />
            </div>

            <button
              type="submit"
              className="btn btn-primary btn-block"
              disabled={registerMutation.isPending}
            >
              {registerMutation.isPending ? 'Регистрация...' : 'Зарегистрироваться'}
            </button>
          </form>

          <p className="auth-note">
            После регистрации вы сможете подключаться к проектам и работать внутри общей среды
            экспериментов.
          </p>

          <div className="auth-switch">
            Уже есть аккаунт? <Link to="/login">Войти</Link>
          </div>
        </div>
      </div>
    </div>
  )
}

export default Register
