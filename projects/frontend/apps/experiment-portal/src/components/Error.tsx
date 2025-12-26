import './Error.css'

interface ErrorProps {
  message: string
}

function Error({ message }: ErrorProps) {
  return <div className="error">{message}</div>
}

export default Error
