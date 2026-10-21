import { describe, it, expect, vi, beforeEach } from 'vitest'
import { formatDateTime } from '../utils/format'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import type { ReactNode } from 'react'
import SensorsList from './SensorsList'
import { sensorsApi, projectsApi } from '../api/client'
import { createMockProject, createMockSensor } from '../testUtils/factories'
import { pickMaterialSelectOption } from '../testUtils/materialSelect'

vi.mock('../api/client', () => ({
    sensorsApi: { list: vi.fn(), getStatusSummary: vi.fn() },
    projectsApi: { list: vi.fn() },
}))

vi.mock('../components/SensorDetailModal', () => ({
    default: ({ sensorId, onClose }: { sensorId: string; onClose: () => void }) => (
        <div data-testid="detail-modal">
            {sensorId}
            <button onClick={onClose}>close-detail</button>
        </div>
    ),
}))

vi.mock('../components/SensorStatusSummaryBar', () => ({
    default: () => <div data-testid="summary-bar" />,
}))

const mockNavigate = vi.fn()
vi.mock('react-router-dom', async () => {
    const actual = await vi.importActual('react-router-dom')
    return { ...actual, useNavigate: () => mockNavigate }
})

const wrapper = ({ children }: { children: ReactNode }) => {
    const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } })
    return (
        <QueryClientProvider client={qc}>
            <MemoryRouter>{children}</MemoryRouter>
        </QueryClientProvider>
    )
}

const oldHb = '2021-06-07T08:09:00Z'
const sensors = [
    createMockSensor({ id: 's1', name: 'Alpha', last_heartbeat: new Date().toISOString(), token_preview: 'zz99', connection_status: 'online' }),
    createMockSensor({ id: 's2', name: 'Beta', last_heartbeat: new Date(Date.now() - 7 * 60_000).toISOString(), token_preview: null, connection_status: undefined }),
    createMockSensor({ id: 's3', name: 'Gamma', last_heartbeat: new Date(Date.now() - 2 * 3_600_000).toISOString() }),
    createMockSensor({ id: 's4', name: 'Delta', last_heartbeat: oldHb }),
    createMockSensor({ id: 's5', name: 'Epsilon', last_heartbeat: null }),
]

describe('SensorsList branches', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        vi.mocked(projectsApi.list).mockResolvedValue({
            projects: [createMockProject({ id: 'p1', name: 'Proj A' })],
            total: 1,
        } as never)
        vi.mocked(sensorsApi.list).mockResolvedValue({ sensors, total: sensors.length, page: 1, page_size: 20 } as never)
        vi.mocked(sensorsApi.getStatusSummary).mockResolvedValue({ total: 1 } as never)
    })

    it('shows projects loading message first', () => {
        vi.mocked(projectsApi.list).mockImplementation(() => new Promise(() => {}))
        render(<SensorsList />, { wrapper })
        expect(screen.getByText('Загрузка проектов...')).toBeInTheDocument()
    })

    it('shows sensors loading message once projects are loaded', async () => {
        vi.mocked(sensorsApi.list).mockImplementation(() => new Promise(() => {}))
        render(<SensorsList />, { wrapper })
        expect(await screen.findByText('Загрузка датчиков...')).toBeInTheDocument()
    })

    it('shows empty projects state', async () => {
        vi.mocked(projectsApi.list).mockResolvedValue({ projects: [], total: 0 } as never)
        render(<SensorsList />, { wrapper })
        expect(await screen.findByText(/У вас нет проектов/)).toBeInTheDocument()
        expect(screen.queryByPlaceholderText('ID или имя сенсора')).not.toBeInTheDocument()
    })

    it('renders heartbeat variants, token preview and live pulse', async () => {
        const { container } = render(<SensorsList />, { wrapper })
        expect(await screen.findByText('Alpha')).toBeInTheDocument()
        expect(screen.getAllByText('Только что')).toHaveLength(2)
        expect(screen.getAllByText('7 мин назад')).toHaveLength(2)
        expect(screen.getAllByText('2 ч назад')).toHaveLength(2)
        expect(screen.getAllByText(formatDateTime(oldHb))).toHaveLength(2)
        expect(screen.getAllByText('Никогда')).toHaveLength(2)
        expect(screen.getByText('****zz99')).toBeInTheDocument()
        expect(container.querySelectorAll('.sensor-card__pulse.is-live')).toHaveLength(4)
        expect(screen.queryByTestId('summary-bar')).not.toBeInTheDocument()
    })

    it('filters by search query and shows empty state', async () => {
        const user = userEvent.setup()
        render(<SensorsList />, { wrapper })
        await screen.findByText('Alpha')
        const search = screen.getByPlaceholderText('ID или имя сенсора')
        await user.type(search, 'GAM')
        expect(screen.getByText('Gamma')).toBeInTheDocument()
        expect(screen.queryByText('Alpha')).not.toBeInTheDocument()
        await user.type(search, 'zzz')
        expect(screen.getByText('Датчики не найдены')).toBeInTheDocument()
    })

    it('opens detail modal via click and keyboard', async () => {
        const user = userEvent.setup()
        render(<SensorsList />, { wrapper })
        // The card's click target is the sensor-name button stretched over it.
        await user.click(await screen.findByRole('button', { name: 'Alpha' }))
        expect(screen.getByTestId('detail-modal')).toHaveTextContent('s1')
        await user.click(screen.getByRole('button', { name: 'close-detail' }))
        expect(screen.queryByTestId('detail-modal')).not.toBeInTheDocument()

        const beta = screen.getByRole('button', { name: 'Beta' })
        beta.focus()
        await user.keyboard('a')
        expect(screen.queryByTestId('detail-modal')).not.toBeInTheDocument()
        await user.keyboard('{Enter}')
        expect(screen.getByTestId('detail-modal')).toHaveTextContent('s2')
        await user.click(screen.getByRole('button', { name: 'close-detail' }))

        const gamma = screen.getByRole('button', { name: 'Gamma' })
        gamma.focus()
        await user.keyboard(' ')
        expect(screen.getByTestId('detail-modal')).toHaveTextContent('s3')
    })

    it('selecting project loads status summary; FAB navigates', async () => {
        const user = userEvent.setup()
        render(<SensorsList />, { wrapper })
        await screen.findByText('Alpha')
        await pickMaterialSelectOption(user, 'Проект', 'Proj A')
        expect(await screen.findByTestId('summary-bar')).toBeInTheDocument()
        expect(sensorsApi.getStatusSummary).toHaveBeenCalledWith('p1')
        await waitFor(() =>
            expect(sensorsApi.list).toHaveBeenCalledWith({ project_id: 'p1', status: undefined, page: 1, page_size: 20 })
        )
        await user.click(screen.getByRole('button', { name: 'Зарегистрировать датчик' }))
        expect(mockNavigate).toHaveBeenCalledWith('/sensors/new')
    })

    it('renders filters but no grid when sensor data has no sensors array', async () => {
        vi.mocked(sensorsApi.list).mockResolvedValue({ total: 0 } as never)
        render(<SensorsList />, { wrapper })
        expect(await screen.findByText('Датчики не найдены')).toBeInTheDocument()
    })
})
