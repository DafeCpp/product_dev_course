import { describe, it, expect, vi, beforeEach } from 'vitest'
import { formatShortDateTime } from '../utils/format'
import { render, screen, within, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import type { ReactNode } from 'react'
import SensorMonitor from './SensorMonitor'
import { sensorsApi, projectsApi } from '../api/client'
import { createMockProject, createMockSensor } from '../testUtils/factories'
import { pickMaterialSelectOption } from '../testUtils/materialSelect'

vi.mock('../api/client', () => ({
    sensorsApi: { list: vi.fn(), getStatusSummary: vi.fn() },
    projectsApi: { list: vi.fn() },
}))

vi.mock('../components/SensorStatusSummaryBar', () => ({
    default: () => <div data-testid="summary-bar" />,
}))

const wrapper = ({ children }: { children: ReactNode }) => {
    const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } })
    return <QueryClientProvider client={qc}>{children}</QueryClientProvider>
}

const oldHb = '2024-01-02T03:04:05Z'

const sensors = [
    createMockSensor({ id: 's1', name: 'Online One', connection_status: 'online', last_heartbeat: new Date().toISOString() }),
    createMockSensor({ id: 's2', name: 'Delayed One', connection_status: 'delayed', last_heartbeat: oldHb }),
    createMockSensor({ id: 's3', name: 'Offline One', connection_status: 'offline', last_heartbeat: null }),
    createMockSensor({ id: 's4', name: 'Unknown One', connection_status: undefined, last_heartbeat: null }),
]

describe('SensorMonitor branches', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        vi.mocked(projectsApi.list).mockResolvedValue({
            projects: [createMockProject({ id: 'p1', name: 'Proj One' })],
            total: 1,
        } as never)
        vi.mocked(sensorsApi.list).mockResolvedValue({ sensors, total: sensors.length } as never)
        vi.mocked(sensorsApi.getStatusSummary).mockResolvedValue({ online: 1, delayed: 1, offline: 1, total: 3 } as never)
    })

    it('shows loading then renders sensor cards with all status variants', async () => {
        const { container } = render(<SensorMonitor />, { wrapper })
        expect(screen.getByText('Загрузка...')).toBeInTheDocument()
        expect(await screen.findByText('Online One')).toBeInTheDocument()
        expect(screen.getByText('Только что')).toBeInTheDocument()
        expect(screen.getAllByText('Никогда')).toHaveLength(2)
        expect(screen.getByText(formatShortDateTime(oldHb, { seconds: true }))).toBeInTheDocument()
        const cards = Array.from(container.querySelectorAll<HTMLElement>('.sensor-monitor__card'))
        expect(cards.map((c) => c.style.borderTopColor)).toEqual([
            'rgb(22, 163, 74)',
            'rgb(217, 119, 6)',
            'rgb(107, 114, 128)',
            'transparent',
        ])
        expect(screen.queryByTestId('summary-bar')).not.toBeInTheDocument()
        expect(sensorsApi.getStatusSummary).not.toHaveBeenCalled()
        expect(screen.queryByText('Датчики не найдены')).not.toBeInTheDocument()
    })

    it('filters by connection status and shows empty state', async () => {
        const user = userEvent.setup()
        vi.mocked(sensorsApi.list).mockResolvedValue({ sensors: sensors.slice(0, 2), total: 2 } as never)
        render(<SensorMonitor />, { wrapper })
        await screen.findByText('Online One')
        await pickMaterialSelectOption(user, 'Подключение', 'Онлайн')
        expect(screen.getByText('Online One')).toBeInTheDocument()
        expect(screen.queryByText('Delayed One')).not.toBeInTheDocument()
        await pickMaterialSelectOption(user, 'Подключение', 'Оффлайн')
        expect(screen.getByText('Датчики не найдены')).toBeInTheDocument()
    })

    it('selects a project and shows status summary', async () => {
        const user = userEvent.setup()
        render(<SensorMonitor />, { wrapper })
        await screen.findByText('Online One')
        await pickMaterialSelectOption(user, 'Проект', 'Proj One')
        expect(await screen.findByTestId('summary-bar')).toBeInTheDocument()
        expect(sensorsApi.getStatusSummary).toHaveBeenCalledWith('p1')
        await waitFor(() =>
            expect(sensorsApi.list).toHaveBeenCalledWith({ project_id: 'p1', limit: 200 })
        )
        expect(sensorsApi.list).toHaveBeenCalledWith({ project_id: undefined, limit: 200 })
    })

    it('shows error state', async () => {
        vi.mocked(sensorsApi.list).mockRejectedValue({ response: { status: 500 } })
        const { container } = render(<SensorMonitor />, { wrapper })
        expect(await screen.findByText('Ошибка загрузки датчиков')).toBeInTheDocument()
        expect(within(container).queryByLabelText('Проект')).not.toBeInTheDocument()
    })

    it('renders when projects fail and sensors list is undefined', async () => {
        vi.mocked(projectsApi.list).mockRejectedValue(new Error('x'))
        vi.mocked(sensorsApi.list).mockResolvedValue({} as never)
        render(<SensorMonitor />, { wrapper })
        expect(await screen.findByText('Датчики не найдены')).toBeInTheDocument()
    })
})
