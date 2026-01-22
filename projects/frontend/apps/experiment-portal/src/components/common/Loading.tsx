import './Loading.scss'

interface LoadingProps {
    message?: string
}

function Loading({ message = 'Загрузка...' }: LoadingProps) {
    return <div className="loading">{message}</div>
}

export default Loading

