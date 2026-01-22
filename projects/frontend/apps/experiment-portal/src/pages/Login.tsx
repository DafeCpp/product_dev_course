import { useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { useMutation, useQueryClient } from '@tanstack/react-query'
import { authApi } from '../api/auth'
import type { LoginRequest } from '../types'
import { IS_TEST } from '../utils/env'
import { notifyError } from '../utils/notify'
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
        onSuccess: async () => {
            // Обновляем кеш пользователя после успешного входа
            await queryClient.invalidateQueries({ queryKey: ['auth', 'me'] })
            // После успешного входа перенаправляем на главную страницу
            navigate('/experiments')
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
        <div className="login-page">
            <div className="login-container">
                <div className="login-card card">
                    <h1 className="login-title">Вход в систему</h1>
                    <p className="login-subtitle">Experiment Tracking Platform</p>

                    {IS_TEST && error && <div className="error-message">{error}</div>}

                    <form onSubmit={handleSubmit} className="login-form">
                        <div className="form-group">
                            <label htmlFor="username">Имя пользователя</label>
                            <input
                                id="username"
                                type="text"
                                value={formData.username}
                                onChange={(e) =>
                                    setFormData({ ...formData, username: e.target.value })
                                }
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
                                onChange={(e) =>
                                    setFormData({ ...formData, password: e.target.value })
                                }
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
                    <div className="auth-switch">
                        Нет аккаунта? <Link to="/register">Зарегистрироваться</Link>
                    </div>
                </div>
            </div>
        </div>
    )
}

export default Login

