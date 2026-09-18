import { Loading as SharedLoading } from '@lostpointer/web-react'

interface LoadingProps {
    message?: string
    showSpinner?: boolean
}

export default function Loading({ message = 'Загрузка...', showSpinner = true }: LoadingProps) {
    return <SharedLoading message={message} showSpinner={showSpinner} />
}
