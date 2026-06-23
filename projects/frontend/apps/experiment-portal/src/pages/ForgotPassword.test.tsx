import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import ForgotPassword from './ForgotPassword'
import { authApi } from '../api/auth'

vi.mock('../api/auth', () => ({
    authApi: {
        requestPasswordReset: vi.fn(),
    },
}))

const renderWithRouter = () =>
    render(
        <MemoryRouter>
            <ForgotPassword />
        </MemoryRouter>,
    )

describe('ForgotPassword', () => {
    beforeEach(() => {
        vi.clearAllMocks()
    })

    it('renders the form with email field and submit button', () => {
        renderWithRouter()

        expect(screen.getByRole('heading', { name: /сброс пароля/i })).toBeInTheDocument()
        expect(screen.getByLabelText(/email/i)).toBeInTheDocument()
        expect(screen.getByRole('button', { name: /отправить ссылку для сброса/i })).toBeInTheDocument()
        expect(screen.getByRole('link', { name: /вернуться ко входу/i })).toBeInTheDocument()
    })

    it('shows validation error when email is empty (form submitted programmatically)', async () => {
        renderWithRouter()

        // type-email field has required attribute, so click submission is short-circuited;
        // submit form directly to bypass HTML5 validation and exercise the trim() check
        const form = screen.getByRole('button', { name: /отправить ссылку для сброса/i }).closest('form')!
        fireEvent.submit(form)

        await waitFor(() => {
            expect(screen.getByText(/введите адрес электронной почты/i)).toBeInTheDocument()
        })
        expect(vi.mocked(authApi.requestPasswordReset)).not.toHaveBeenCalled()
    })

    it('submits trimmed email and shows success message on success', async () => {
        vi.mocked(authApi.requestPasswordReset).mockResolvedValueOnce(undefined)
        const user = userEvent.setup()
        renderWithRouter()

        await user.type(screen.getByLabelText(/email/i), '  alice@example.com  ')
        await user.click(screen.getByRole('button', { name: /отправить ссылку для сброса/i }))

        await waitFor(() => {
            expect(vi.mocked(authApi.requestPasswordReset)).toHaveBeenCalledWith('alice@example.com')
            expect(screen.getByText(/проверьте почту/i)).toBeInTheDocument()
        })
    })

    it('shows API error message on failure', async () => {
        vi.mocked(authApi.requestPasswordReset).mockRejectedValueOnce({
            response: { data: { error: 'rate limit exceeded' } },
        })
        const user = userEvent.setup()
        renderWithRouter()

        await user.type(screen.getByLabelText(/email/i), 'alice@example.com')
        await user.click(screen.getByRole('button', { name: /отправить ссылку для сброса/i }))

        await waitFor(() => {
            expect(screen.getByText(/rate limit exceeded/i)).toBeInTheDocument()
        })
        // Form remains visible (not switched to success view)
        expect(screen.getByLabelText(/email/i)).toBeInTheDocument()
    })

    it('falls back to generic error message when API returns no error field', async () => {
        vi.mocked(authApi.requestPasswordReset).mockRejectedValueOnce({})
        const user = userEvent.setup()
        renderWithRouter()

        await user.type(screen.getByLabelText(/email/i), 'alice@example.com')
        await user.click(screen.getByRole('button', { name: /отправить ссылку для сброса/i }))

        await waitFor(() => {
            expect(screen.getByText(/не удалось отправить запрос/i)).toBeInTheDocument()
        })
    })

    it('disables submit button while pending', async () => {
        let resolveRequest: (v: any) => void
        const pending = new Promise((resolve) => {
            resolveRequest = resolve
        })
        vi.mocked(authApi.requestPasswordReset).mockReturnValueOnce(pending as any)
        const user = userEvent.setup()
        renderWithRouter()

        await user.type(screen.getByLabelText(/email/i), 'alice@example.com')
        await user.click(screen.getByRole('button', { name: /отправить ссылку для сброса/i }))

        await waitFor(() => {
            expect(screen.getByRole('button', { name: /отправка/i })).toBeDisabled()
        })

        resolveRequest!(undefined)
    })
})
