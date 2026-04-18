import { useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { useMutation, useQueryClient } from '@tanstack/react-query'
import { authApi } from '../api/auth'
import { notifyError, notifySuccess } from '../utils/notify'

function ChangePassword() {
  const navigate = useNavigate()
  const queryClient = useQueryClient()
  const [oldPassword, setOldPassword] = useState('')
  const [newPassword, setNewPassword] = useState('')
  const [confirmPassword, setConfirmPassword] = useState('')
  const [error, setError] = useState<string | null>(null)

  const mutation = useMutation({
    mutationFn: () => authApi.changePassword({ old_password: oldPassword, new_password: newPassword }),
    onSuccess: async () => {
      // Invalidate auth/me so password_change_required flag is refreshed
      await queryClient.invalidateQueries({ queryKey: ['auth', 'me'] })
      notifySuccess('Пароль успешно изменён')
      navigate('/experiments', { replace: true })
    },
    onError: (err: any) => {
      const msg =
        err.response?.data?.error ||
        err.response?.data?.message ||
        'Ошибка смены пароля. Проверьте текущий пароль.'
      setError(msg)
      notifyError(msg)
    },
  })

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault()
    setError(null)

    if (!oldPassword) {
      const msg = 'Введите текущий пароль'
      setError(msg)
      notifyError(msg)
      return
    }

    if (!newPassword) {
      const msg = 'Введите новый пароль'
      setError(msg)
      notifyError(msg)
      return
    }

    if (newPassword !== confirmPassword) {
      const msg = 'Пароли не совпадают'
      setError(msg)
      notifyError(msg)
      return
    }

    if (newPassword.length < 8) {
      const msg = 'Новый пароль должен содержать не менее 8 символов'
      setError(msg)
      notifyError(msg)
      return
    }

    mutation.mutate()
  }

  return (
    <div className="auth-page">
      <div className="auth-shell">
        <div className="login-card card auth-card">
          <div className="auth-card__intro">
            <h2 className="login-title">Смена пароля</h2>
            <p className="login-subtitle">
              Ваш аккаунт требует смены пароля перед продолжением работы.
            </p>
          </div>

          {error && <div className="error-message">{error}</div>}

          <form onSubmit={handleSubmit} className="login-form auth-form">
            <div className="form-group">
              <label htmlFor="old_password">Текущий пароль</label>
              <input
                id="old_password"
                type="password"
                value={oldPassword}
                onChange={(e) => setOldPassword(e.target.value)}
                required
                autoComplete="current-password"
                placeholder="Введите текущий пароль"
                disabled={mutation.isPending}
              />
            </div>

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
                disabled={mutation.isPending}
              />
            </div>

            <div className="form-group">
              <label htmlFor="confirm_password">Подтверждение нового пароля</label>
              <input
                id="confirm_password"
                type="password"
                value={confirmPassword}
                onChange={(e) => setConfirmPassword(e.target.value)}
                required
                autoComplete="new-password"
                placeholder="Повторите новый пароль"
                disabled={mutation.isPending}
              />
            </div>

            <button
              type="submit"
              className="btn btn-primary btn-block"
              disabled={mutation.isPending}
            >
              {mutation.isPending ? 'Сохранение...' : 'Сменить пароль'}
            </button>
          </form>
        </div>
      </div>
    </div>
  )
}

export default ChangePassword
