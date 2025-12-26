import './Loading.css'

interface LoadingProps {
  message?: string
}

function Loading({ message = 'Загрузка...' }: LoadingProps) {
  return <div className="loading">{message}</div>
}

export default Loading
