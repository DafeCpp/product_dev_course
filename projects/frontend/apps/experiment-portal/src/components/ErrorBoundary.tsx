import { Component, type ErrorInfo, type ReactNode } from 'react'
import { Link } from 'react-router-dom'
import { ErrorState } from '@lostpointer/web-react'

interface ErrorBoundaryProps {
    children: ReactNode
    // A change of this key (e.g. the current path) clears a caught error, so navigating away recovers.
    resetKey?: string
}

interface ErrorBoundaryState {
    error: Error | null
    resetKey?: string
}

// Catches render errors (and failed lazy page loads) below it and shows a fallback
// with "retry" and "back" actions instead of a blank screen.
export default class ErrorBoundary extends Component<ErrorBoundaryProps, ErrorBoundaryState> {
    state: ErrorBoundaryState = { error: null, resetKey: this.props.resetKey }

    static getDerivedStateFromError(error: Error): Partial<ErrorBoundaryState> {
        return { error }
    }

    static getDerivedStateFromProps(
        props: ErrorBoundaryProps,
        state: ErrorBoundaryState,
    ): Partial<ErrorBoundaryState> | null {
        if (props.resetKey !== state.resetKey) {
            return { error: null, resetKey: props.resetKey }
        }
        return null
    }

    componentDidCatch(error: Error, info: ErrorInfo) {
        console.error('Unhandled render error', error, info.componentStack)
    }

    private retry = () => {
        this.setState({ error: null })
    }

    render() {
        const { error } = this.state
        if (!error) return this.props.children
        return (
            <ErrorState
                title="Что-то пошло не так"
                message={error.message || 'Не удалось отобразить страницу.'}
                onRetry={this.retry}
                retryLabel="Повторить"
                action={
                    <Link to="/experiments" className="btn btn-secondary">
                        К экспериментам
                    </Link>
                }
            />
        )
    }
}
