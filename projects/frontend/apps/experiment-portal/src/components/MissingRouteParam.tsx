import { Link } from 'react-router-dom'
import { ErrorState } from '@lostpointer/web-react'

// Shown instead of a detail page when the URL lacks the id the page needs,
// so no request is ever sent with an undefined id.
export default function MissingRouteParam() {
    return (
        <ErrorState
            title="Некорректная ссылка"
            message="В адресе не указан идентификатор."
            action={
                <Link to="/experiments" className="btn btn-secondary">
                    К экспериментам
                </Link>
            }
        />
    )
}
