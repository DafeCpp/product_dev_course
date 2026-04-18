import { useState } from 'react'
import { Link, useNavigate, useSearchParams } from 'react-router-dom'
import { authApi } from '../api/auth'
import { notifySuccess } from '../utils/notify'
import './Login.scss'

function ResetPassword() {
  const [searchParams] = useSearchParams()
  const token = searchParams.get('token')
  const navigate = useNavigate()

  const [newPassword, setNewPassword] = useState('')
  const [confirmPassword, setConfirmPassword] = useState('')
  const [error, setError] = useState<string | null>(null)
  const [isPending, setIsPending] = useState(false)

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault()
    setError(null)

    if (!newPassword) {
      setError('Введите новый пароль')
      return
    }

    if (newPassword.length < 8) {
      setError('Пароль должен содержать не менее 8 символов')
      return
    }

    if (newPassword !== confirmPassword) {
      setError('Пароли не совпадают')
      return
    }

    setIsPending(true)
    try {
      await authApi.confirmPasswordReset(token!, newPassword)
      notifySuccess('Пароль успешно изменён')
      navigate('/login', { replace: true })
    } catch (err: any) {
      const msg =
        err.response?.data?.error ||
        err.response?.data?.message ||
        'Не удалось сбросить пароль. Возможно, ссылка устарела или недействительна.'
      setError(msg)
    } finally {
      setIsPending(false)
    }
  }

  if (!token) {
    return (
      <div className="auth-page">
        <div className="auth-shell">
          <div className="login-card card auth-card">
            <div className="auth-card__intro">
              <h2 className="login-title">Сброс пароля</h2>
            </div>
            <div className="error-message">Недействительная ссылка для сброса пароля</div>
            <div className="auth-switch">
              <Link to="/login">← Вернуться ко входу</Link>
            </div>
          </div>
        </div>
      </div>
    )
  }

  return (
    <div className="auth-page">
      <div className="auth-shell">
        <div className="login-card card auth-card">
          <div className="auth-card__intro">
            <h2 className="login-title">Новый пароль</h2>
            <p className="login-subtitle">Придумайте новый пароль для вашего аккаунта.</p>
          </div>

          {error && <div className="error-message">{error}</div>}

          <form onSubmit={handleSubmit} className="login-form auth-form">
            <div className="form-group">
              <label htmlFor="new_password">Новый пароль</label>
              <input
                id="new_password"
                type="password"
                value={newPassword}
                onChange={(e) => setNewPassword(e.target.value)}
                required
                autoComplete="new-password"
                placeholder="Не менее 8 символов"
                disabled={isPending}
              />
            </div>

            <div className="form-group">
              <label htmlFor="confirm_password">Подтвердите пароль</label>
              <input
                id="confirm_password"
                type="password"
                value={confirmPassword}
                onChange={(e) => setConfirmPassword(e.target.value)}
                required
                autoComplete="new-password"
                placeholder="Повторите новый пароль"
                disabled={isPending}
              />
            </div>

            <button
              type="submit"
              className="btn btn-primary btn-block"
              disabled={isPending}
            >
              {isPending ? 'Сохранение...' : 'Установить новый пароль'}
            </button>
          </form>

          <div className="auth-switch">
            <Link to="/login">← Вернуться ко входу</Link>
          </div>
        </div>
      </div>
    </div>
  )
}

export default ResetPassword
