import { Component, type ErrorInfo, type ReactNode } from 'react'
import { Link } from 'react-router-dom'
import { ErrorState } from '@lostpointer/web-react'

// A page chunk that failed to download: React caches the rejected lazy import,
// so only a full reload fetches it again.
const CHUNK_LOAD_ERROR =
    /dynamically imported module|Importing a module script failed|Loading (CSS )?chunk .* failed/i

export function isChunkLoadError(error: Error): boolean {
    return error.name === 'ChunkLoadError' || CHUNK_LOAD_ERROR.test(error.message)
}

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
        if (this.state.error && isChunkLoadError(this.state.error)) {
            window.location.reload()
            return
        }
        this.setState({ error: null })
    }

    // The link may point at the current path, where the reset key does not change.
    private leave = () => {
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
                    <Link to="/experiments" className="btn btn-secondary" onClick={this.leave}>
                        К экспериментам
                    </Link>
                }
            />
        )
    }
}
