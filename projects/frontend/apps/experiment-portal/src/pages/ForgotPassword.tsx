import { useState } from 'react'
import { Link } from 'react-router-dom'
import { authApi } from '../api/auth'
import './Login.scss'

function ForgotPassword() {
  const [email, setEmail] = useState('')
  const [submitted, setSubmitted] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [isPending, setIsPending] = useState(false)

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault()
    setError(null)

    if (!email.trim()) {
      setError('Введите адрес электронной почты')
      return
    }

    setIsPending(true)
    try {
      await authApi.requestPasswordReset(email.trim())
      setSubmitted(true)
    } catch (err: any) {
      const msg =
        err.response?.data?.error ||
        err.response?.data?.message ||
        'Не удалось отправить запрос. Попробуйте позже.'
      setError(msg)
    } finally {
      setIsPending(false)
    }
  }

  return (
    <div className="auth-page">
      <div className="auth-shell">
        <div className="login-card card auth-card">
          <div className="auth-card__intro">
            <h2 className="login-title">Сброс пароля</h2>
            <p className="login-subtitle">
              Введите email вашего аккаунта, и мы отправим ссылку для сброса пароля.
            </p>
          </div>

          {submitted ? (
            <div className="auth-success-message">
              Проверьте почту. Если этот email зарегистрирован, вы получите письмо.
            </div>
          ) : (
            <>
              {error && <div className="error-message">{error}</div>}

              <form onSubmit={handleSubmit} className="login-form auth-form">
                <div className="form-group">
                  <label htmlFor="email">Email</label>
                  <input
                    id="email"
                    type="email"
                    value={email}
                    onChange={(e) => setEmail(e.target.value)}
                    required
                    autoComplete="email"
                    placeholder="Введите адрес электронной почты"
                    disabled={isPending}
                  />
                </div>

                <button
                  type="submit"
                  className="btn btn-primary btn-block"
                  disabled={isPending}
                >
                  {isPending ? 'Отправка...' : 'Отправить ссылку для сброса'}
                </button>
              </form>
            </>
          )}

          <div className="auth-switch">
            <Link to="/login">← Вернуться ко входу</Link>
          </div>
        </div>
      </div>
    </div>
  )
}

export default ForgotPassword
