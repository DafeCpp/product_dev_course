import { ErrorState } from '@lostpointer/web-react'

export default function Error({ message }: { message: string }) {
    return <ErrorState message={message} />
}
