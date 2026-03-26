import { useState } from 'react'
import { Link, useNavigate, useSearchParams } from 'react-router-dom'
import { useMutation } from '@tanstack/react-query'
import { authApi } from '../api/auth'
import type { RegisterRequest } from '../types'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'
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

  const registerMutation = useMutation({
    mutationFn: (payload: RegisterRequest) => authApi.register(payload),
    onSuccess: () => {
      notifySuccess('Регистрация успешна. Войдите в систему.')
      navigate('/login')
    },
    onError: (err: any) => {
      const msg =
        err.response?.data?.error ||
        err.response?.data?.message ||
        'Ошибка регистрации. Проверьте введенные данные.'
      setError(msg)
      notifyError(msg)
    },
  })

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault()
    setError(null)

    const username = formData.username.trim()
    const email = formData.email.trim()

    if (!username) {
      const msg = 'Введите имя пользователя'
      setError(msg)
      notifyError(msg)
      return
    }

    if (username.length < 3) {
      const msg = 'Имя пользователя должно быть не короче 3 символов'
      setError(msg)
      notifyError(msg)
      return
    }

    if (!email) {
      const msg = 'Введите email'
      setError(msg)
      notifyError(msg)
      return
    }

    if (!/^\S+@\S+\.\S+$/.test(email)) {
      const msg = 'Введите корректный email'
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

    if (formData.password.length < 8) {
      const msg = 'Пароль должен быть не короче 8 символов'
      setError(msg)
      notifyError(msg)
      return
    }

    if (!formData.confirmPassword) {
      const msg = 'Повторите пароль'
      setError(msg)
      notifyError(msg)
      return
    }

    if (formData.password !== formData.confirmPassword) {
      const msg = 'Пароли не совпадают'
      setError(msg)
      notifyError(msg)
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

          {IS_TEST && error && <div className="error-message">{error}</div>}

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
