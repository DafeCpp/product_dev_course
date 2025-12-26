import { ReactNode } from 'react'
import { Navigate } from 'react-router-dom'
import { useQuery } from '@tanstack/react-query'
import { authApi } from '../api/auth'
import './ProtectedRoute.css'

interface ProtectedRouteProps {
    children: ReactNode
}

/**
 * Компонент для защиты роутов
 * Проверяет авторизацию пользователя через /auth/me
 * Если не авторизован - перенаправляет на /login
 */
function ProtectedRoute({ children }: ProtectedRouteProps) {
    const { data: user, isLoading, isError } = useQuery({
        queryKey: ['auth', 'me'],
        queryFn: () => authApi.me(),
        retry: false,
        staleTime: 5 * 60 * 1000, // 5 минут
    })

    if (isLoading) {
        return (
            <div className="loading-container">
                <div className="loading">Проверка авторизации...</div>
            </div>
        )
    }

    if (isError || !user) {
        return <Navigate to="/login" replace />
    }

    return <>{children}</>
}

export default ProtectedRoute

