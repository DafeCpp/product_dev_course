import { ReactNode } from 'react'
import { Navigate } from 'react-router-dom'
import { useQuery } from '@tanstack/react-query'
import { authApi } from '../api/auth'
import './ProtectedRoute.scss'

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
        staleTime: 0,
        refetchOnMount: 'always',
        refetchOnWindowFocus: true,
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

