import { ReactNode } from 'react'
import { Navigate, useLocation } from 'react-router-dom'
import { useQuery } from '@tanstack/react-query'
import { authApi } from '../api/auth'
import './ProtectedRoute.scss'

interface ProtectedRouteProps {
    children: ReactNode
    /** Если true — этот роут доступен только при password_change_required=false */
    requirePasswordChanged?: boolean
}

/**
 * Компонент для защиты роутов
 * Проверяет авторизацию пользователя через /auth/me
 * Если не авторизован - перенаправляет на /login
 * Если password_change_required - перенаправляет на /change-password
 */
function ProtectedRoute({ children, requirePasswordChanged = true }: ProtectedRouteProps) {
    const location = useLocation()
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

    // If password change is required and the user is not already on /change-password
    if (requirePasswordChanged && user.password_change_required && location.pathname !== '/change-password') {
        return <Navigate to="/change-password" replace />
    }

    return <>{children}</>
}

export default ProtectedRoute

