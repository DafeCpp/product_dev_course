import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import { fireEvent } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { MemoryRouter } from 'react-router-dom'
import Webhooks from './Webhooks'
import { webhooksApi } from '../api/client'

// Webhooks page imports webhooksApi from '../api/client' (re-exported from './webhooks')
vi.mock('../api/client', () => ({
    webhooksApi: {
        list: vi.fn(),
        create: vi.fn(),
        delete: vi.fn(),
        listDeliveries: vi.fn(),
        retryDelivery: vi.fn(),
    },
    // other named exports that might be imported transitively
    experimentsApi: { list: vi.fn(), search: vi.fn() },
    projectsApi: { list: vi.fn() },
    apiGet: vi.fn(),
    apiPost: vi.fn(),
    apiDelete: vi.fn(),
    apiPatch: vi.fn(),
}))

vi.mock('../utils/notify', () => ({
    notifyError: vi.fn(),
    notifySuccess: vi.fn(),
}))

const emptyDeliveries = { deliveries: [], total: 0, page: 1, page_size: 20 }
const emptySubscriptions = { webhooks: [], total: 0, page: 1, page_size: 100 }

function createWrapper() {
    const queryClient = new QueryClient({
        defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
    })
    return ({ children }: { children: React.ReactNode }) => (
        <QueryClientProvider client={queryClient}>
            <MemoryRouter>{children}</MemoryRouter>
        </QueryClientProvider>
    )
}

const mockWebhook = {
    id: 'wh-1',
    project_id: 'proj-1',
    target_url: 'https://example.com/hook',
    secret: null,
    event_types: ['run.started', 'run.finished'],
    is_active: true,
    created_at: '2024-01-01T00:00:00Z',
    updated_at: '2024-01-01T00:00:00Z',
}

const mockDelivery = {
    id: 'del-1',
    subscription_id: 'wh-1',
    project_id: 'proj-1',
    event_type: 'run.started',
    target_url: 'https://example.com/hook',
    secret: null,
    request_body: {},
    status: 'delivered',
    attempt_count: 1,
    last_error: null,
    dedup_key: null,
    locked_at: null,
    next_attempt_at: '2024-01-01T00:00:00Z',
    created_at: '2024-01-15T10:30:00Z',
    updated_at: '2024-01-15T10:30:00Z',
}

describe('Webhooks', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        vi.mocked(webhooksApi.listDeliveries).mockResolvedValue(emptyDeliveries)
    })

    // -----------------------------------------------------------------------
    // 1. Header + empty state
    // -----------------------------------------------------------------------
    it('renders header and empty state when no subscriptions', async () => {
        vi.mocked(webhooksApi.list).mockResolvedValue(emptySubscriptions)

        render(<Webhooks />, { wrapper: createWrapper() })

        expect(await screen.findByText('Webhook-подписки')).toBeInTheDocument()
        expect(await screen.findByText('Webhook-подписок пока нет')).toBeInTheDocument()
        // button to create first webhook should appear in empty state
        expect(screen.getByRole('button', { name: /создать первый webhook/i })).toBeInTheDocument()
    })

    // -----------------------------------------------------------------------
    // 2. Lists subscriptions with status badges and event-type chips
    // -----------------------------------------------------------------------
    it('lists subscriptions with status badge and event-type chips', async () => {
        vi.mocked(webhooksApi.list).mockResolvedValue({
            webhooks: [mockWebhook],
            total: 1,
            page: 1,
            page_size: 100,
        })

        render(<Webhooks />, { wrapper: createWrapper() })

        expect(await screen.findByText('https://example.com/hook')).toBeInTheDocument()
        expect(screen.getByText('Активен')).toBeInTheDocument()
        expect(screen.getByText('run.started')).toBeInTheDocument()
        expect(screen.getByText('run.finished')).toBeInTheDocument()
    })

    // -----------------------------------------------------------------------
    // 3. Create form — valid submission calls webhooksApi.create
    // -----------------------------------------------------------------------
    it('opens create form, fills fields, submits and calls webhooksApi.create', async () => {
        const user = userEvent.setup()
        vi.mocked(webhooksApi.list).mockResolvedValue(emptySubscriptions)
        vi.mocked(webhooksApi.create).mockResolvedValue(mockWebhook)

        render(<Webhooks />, { wrapper: createWrapper() })

        // Open form
        await user.click(screen.getByRole('button', { name: /создать$/i }))

        expect(await screen.findByLabelText(/target url/i)).toBeInTheDocument()

        // Use fireEvent.change to avoid jsdom cssstyle bug with
        // border: 1px solid var(--outline) in form-group elements on focus
        fireEvent.change(screen.getByLabelText(/target url/i), {
            target: { value: 'https://example.com/hook' },
        })
        fireEvent.change(screen.getByLabelText(/типы событий/i), {
            target: { value: 'run.started, run.finished' },
        })
        await user.click(screen.getByRole('button', { name: /создать webhook/i }))

        await waitFor(() => {
            expect(vi.mocked(webhooksApi.create)).toHaveBeenCalledWith(
                expect.objectContaining({
                    target_url: 'https://example.com/hook',
                    event_types: ['run.started', 'run.finished'],
                })
            )
        })
    })

    // -----------------------------------------------------------------------
    // 4. Validation error: invalid URL → field error shown, no API call
    // -----------------------------------------------------------------------
    it('shows field error for invalid URL and does not call API', async () => {
        const user = userEvent.setup()
        vi.mocked(webhooksApi.list).mockResolvedValue(emptySubscriptions)

        render(<Webhooks />, { wrapper: createWrapper() })

        await user.click(screen.getByRole('button', { name: /создать$/i }))
        await screen.findByLabelText(/target url/i)

        // Use fireEvent.change to avoid jsdom cssstyle bug with
        // border: 1px solid var(--outline) in form-group elements on focus
        fireEvent.change(screen.getByLabelText(/target url/i), {
            target: { value: 'not-a-url' },
        })
        fireEvent.change(screen.getByLabelText(/типы событий/i), {
            target: { value: 'run.started' },
        })
        await user.click(screen.getByRole('button', { name: /создать webhook/i }))

        // Field error text should appear
        await waitFor(() => {
            expect(screen.getByText(/корректный url/i)).toBeInTheDocument()
        })
        expect(vi.mocked(webhooksApi.create)).not.toHaveBeenCalled()
    })

    // -----------------------------------------------------------------------
    // 5. Delete subscription: confirm=true calls webhooksApi.delete
    // -----------------------------------------------------------------------
    it('deletes subscription when confirm is accepted', async () => {
        const user = userEvent.setup()
        vi.stubGlobal('confirm', vi.fn(() => true))
        vi.mocked(webhooksApi.list).mockResolvedValue({
            webhooks: [mockWebhook],
            total: 1,
            page: 1,
            page_size: 100,
        })
        vi.mocked(webhooksApi.delete).mockResolvedValue(undefined)

        render(<Webhooks />, { wrapper: createWrapper() })

        const deleteBtn = await screen.findByRole('button', { name: /удалить/i })
        await user.click(deleteBtn)

        await waitFor(() => {
            expect(vi.mocked(webhooksApi.delete)).toHaveBeenCalledWith('wh-1')
        })
    })

    // -----------------------------------------------------------------------
    // 6. Deliveries table renders rows; status filter change refetches
    // -----------------------------------------------------------------------
    it('renders deliveries table and refetches on status filter change', async () => {
        const user = userEvent.setup()
        vi.mocked(webhooksApi.list).mockResolvedValue(emptySubscriptions)
        vi.mocked(webhooksApi.listDeliveries).mockResolvedValue({
            deliveries: [mockDelivery],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<Webhooks />, { wrapper: createWrapper() })

        // Delivery event type should appear in the table
        expect(await screen.findByText('run.started')).toBeInTheDocument()
        expect(screen.getByText('delivered')).toBeInTheDocument()

        // Change status filter
        const select = screen.getByLabelText(/статус:/i)
        await user.selectOptions(select, 'failed')

        await waitFor(() => {
            const calls = vi.mocked(webhooksApi.listDeliveries).mock.calls
            const callWithFilter = calls.find(
                (c) => c[0]?.status === 'failed'
            )
            expect(callWithFilter).toBeDefined()
        })
    })

    // -----------------------------------------------------------------------
    // 7. Retry button on failed delivery calls webhooksApi.retryDelivery
    // -----------------------------------------------------------------------
    it('retry button on failed delivery calls retryDelivery', async () => {
        const user = userEvent.setup()
        const failedDelivery = {
            ...mockDelivery,
            id: 'del-2',
            status: 'failed',
            last_error: 'Connection refused',
        }
        vi.mocked(webhooksApi.list).mockResolvedValue(emptySubscriptions)
        vi.mocked(webhooksApi.listDeliveries).mockResolvedValue({
            deliveries: [failedDelivery],
            total: 1,
            page: 1,
            page_size: 20,
        })
        vi.mocked(webhooksApi.retryDelivery).mockResolvedValue(undefined)

        render(<Webhooks />, { wrapper: createWrapper() })

        const retryBtn = await screen.findByRole('button', { name: /retry/i })
        await user.click(retryBtn)

        await waitFor(() => {
            expect(vi.mocked(webhooksApi.retryDelivery)).toHaveBeenCalledWith('del-2')
        })
    })
})
