import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { format } from 'date-fns'
import type { ReactNode } from 'react'
import SensorDetail from './SensorDetail'
import { sensorsApi, projectsApi, conversionProfilesApi, backfillApi } from '../api/client'
import { notifyError, notifySuccessSticky } from '../utils/notify'
import { createMockSensor } from '../testUtils/factories'
import type { BackfillTasksListResponse, ConversionProfilesListResponse } from '../types'

vi.mock('../api/client', () => ({
    sensorsApi: {
        get: vi.fn(),
        delete: vi.fn(),
        rotateToken: vi.fn(),
        getProjects: vi.fn(),
        addProject: vi.fn(),
        removeProject: vi.fn(),
        getErrorLog: vi.fn(),
    },
    projectsApi: { list: vi.fn() },
    conversionProfilesApi: { list: vi.fn(), publish: vi.fn() },
    backfillApi: { list: vi.fn(), start: vi.fn() },
}))

vi.mock('../utils/notify', () => ({
    notifyError: vi.fn(),
    notifySuccess: vi.fn(),
    notifySuccessSticky: vi.fn(),
}))

vi.mock('../components/TestTelemetryModal', () => ({
    default: ({ isOpen, onClose, sensorToken }: { isOpen: boolean; onClose: () => void; sensorToken: string | null }) =>
        isOpen ? (
            <div data-testid="test-telemetry-modal">
                token:{sensorToken ?? 'none'}
                <button onClick={onClose}>close-test</button>
            </div>
        ) : null,
}))
vi.mock('../components/TelemetryStreamModal', () => ({
    default: ({ isOpen, onClose }: { isOpen: boolean; onClose: () => void }) =>
        isOpen ? (
            <div data-testid="stream-modal">
                <button onClick={onClose}>close-stream</button>
            </div>
        ) : null,
}))
vi.mock('../components/ConversionProfileCreateModal', () => ({
    default: ({ isOpen, onClose }: { isOpen: boolean; onClose: () => void }) =>
        isOpen ? (
            <div data-testid="profile-modal">
                <button onClick={onClose}>close-profile</button>
            </div>
        ) : null,
}))

vi.mock('react-router-dom', async () => {
    const actual = await vi.importActual('react-router-dom')
    return {
        ...actual,
        useNavigate: () => vi.fn(),
        useParams: () => ({ id: 'sensor-1' }),
    }
})

const wrapper = ({ children }: { children: ReactNode }) => {
    const queryClient = new QueryClient({
        defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
    })
    return (
        <QueryClientProvider client={queryClient}>
            <MemoryRouter>{children}</MemoryRouter>
        </QueryClientProvider>
    )
}

const baseSensor = createMockSensor({
    id: 'sensor-1',
    name: 'Branch Sensor',
    token_preview: null,
    active_profile_id: null,
    calibration_notes: null,
    last_heartbeat: null,
})

const originalClipboard = Object.getOwnPropertyDescriptor(navigator, 'clipboard')

describe('SensorDetail branches', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        vi.mocked(sensorsApi.getProjects).mockResolvedValue({ project_ids: [] } as never)
        vi.mocked(projectsApi.list).mockResolvedValue({ projects: [], total: 0 } as never)
        vi.mocked(conversionProfilesApi.list).mockResolvedValue({
            conversion_profiles: [],
            total: 0,
        } as unknown as ConversionProfilesListResponse)
        vi.mocked(backfillApi.list).mockResolvedValue({ backfill_tasks: [], total: 0 } as unknown as BackfillTasksListResponse)
        vi.mocked(sensorsApi.getErrorLog).mockResolvedValue({
            sensor_id: 'sensor-1',
            entries: [],
            total: 0,
            limit: 25,
            offset: 0,
        })
    })

    afterEach(() => {
        if (originalClipboard) {
            Object.defineProperty(navigator, 'clipboard', originalClipboard)
        }
        vi.restoreAllMocks()
    })

    it('renders without optional fields and "never" heartbeat', async () => {
        vi.mocked(sensorsApi.get).mockResolvedValueOnce(baseSensor)
        render(<SensorDetail />, { wrapper })
        expect(await screen.findByText('Branch Sensor')).toBeInTheDocument()
        expect(screen.getByText('Никогда')).toBeInTheDocument()
        expect(screen.queryByText('Токен (превью)')).not.toBeInTheDocument()
        expect(screen.queryByText('Активный профиль преобразования')).not.toBeInTheDocument()
        expect(screen.queryByText('Заметки по калибровке')).not.toBeInTheDocument()
        expect(screen.getByRole('button', { name: 'Запустить пересчёт' })).toBeDisabled()
    })

    it('shows generic error when sensor is missing without error', async () => {
        vi.mocked(sensorsApi.get).mockResolvedValueOnce(null as never)
        render(<SensorDetail />, { wrapper })
        expect(await screen.findByText('Не удалось загрузить датчик')).toBeInTheDocument()
    })

    it.each([
        [10 * 1000, 'Только что'],
        [5 * 60 * 1000, '5 мин назад'],
        [3 * 60 * 60 * 1000, '3 ч назад'],
    ])('formats heartbeat %d ms ago', async (offset, expected) => {
        vi.mocked(sensorsApi.get).mockResolvedValueOnce({
            ...baseSensor,
            last_heartbeat: new Date(Date.now() - offset).toISOString(),
        })
        render(<SensorDetail />, { wrapper })
        expect(await screen.findByText(expected)).toBeInTheDocument()
    })

    it('formats old heartbeat as date', async () => {
        const hb = '2020-05-06T07:08:09Z'
        vi.mocked(sensorsApi.get).mockResolvedValueOnce({ ...baseSensor, last_heartbeat: hb })
        render(<SensorDetail />, { wrapper })
        expect(await screen.findByText(format(new Date(hb), 'dd MMM yyyy HH:mm:ss'))).toBeInTheDocument()
    })

    it('polls backfill while tasks are active', async () => {
        vi.mocked(sensorsApi.get).mockResolvedValueOnce({ ...baseSensor, active_profile_id: 'p1' })
        vi.mocked(backfillApi.list).mockResolvedValue({
            backfill_tasks: [
                {
                    id: 't1',
                    sensor_id: 'sensor-1',
                    status: 'running',
                    total_records: 10,
                    processed_records: 1,
                    error_message: null,
                    created_at: '2024-01-01T00:00:00Z',
                },
            ],
            total: 1,
        } as unknown as BackfillTasksListResponse)
        render(<SensorDetail />, { wrapper })
        expect(await screen.findByText('Выполняется')).toBeInTheDocument()
        expect(screen.getByRole('button', { name: 'Запустить пересчёт' })).toBeEnabled()
    })

    it('opens and closes the stub modals', async () => {
        const user = userEvent.setup()
        vi.mocked(sensorsApi.get).mockResolvedValueOnce(baseSensor)
        render(<SensorDetail />, { wrapper })
        await screen.findByText('Branch Sensor')

        await user.click(screen.getByRole('button', { name: 'Тестовая отправка' }))
        expect(screen.getByTestId('test-telemetry-modal')).toHaveTextContent('token:none')
        await user.click(screen.getByRole('button', { name: 'close-test' }))
        expect(screen.queryByTestId('test-telemetry-modal')).not.toBeInTheDocument()

        await user.click(screen.getByRole('button', { name: 'Live telemetry' }))
        await user.click(screen.getByRole('button', { name: 'close-stream' }))
        expect(screen.queryByTestId('stream-modal')).not.toBeInTheDocument()

        await user.click(await screen.findByRole('button', { name: 'Создать профиль' }))
        await user.click(screen.getByRole('button', { name: 'close-profile' }))
        expect(screen.queryByTestId('profile-modal')).not.toBeInTheDocument()
    })

    const rotate = async (user: ReturnType<typeof userEvent.setup>) => {
        vi.mocked(sensorsApi.get).mockResolvedValue(baseSensor)
        vi.mocked(sensorsApi.rotateToken).mockResolvedValueOnce({ sensor: baseSensor, token: 'tok-123' })
        render(<SensorDetail />, { wrapper })
        await screen.findByText('Branch Sensor')
        await user.click(screen.getByRole('button', { name: 'Ротация токена' }))
        expect(await screen.findByText('tok-123')).toBeInTheDocument()
    }

    it('falls back to execCommand when clipboard API is missing, then closes token', async () => {
        const user = userEvent.setup()
        await rotate(user)
        Object.defineProperty(navigator, 'clipboard', { value: undefined, configurable: true })
        const exec = vi.fn().mockReturnValue(true)
        Object.defineProperty(document, 'execCommand', { value: exec, configurable: true, writable: true })

        await user.click(screen.getByRole('button', { name: 'Копировать' }))
        await waitFor(() => expect(notifySuccessSticky).toHaveBeenCalled())
        expect(exec).toHaveBeenCalledWith('copy')

        await user.click(screen.getByRole('button', { name: 'Тестовая отправка' }))
        expect(screen.getByTestId('test-telemetry-modal')).toHaveTextContent('token:tok-123')

        await user.click(screen.getByRole('button', { name: 'Закрыть' }))
        expect(screen.queryByText('tok-123', { selector: 'code' })).not.toBeInTheDocument()
    })

    it('notifies error when both clipboard and execCommand fail', async () => {
        const user = userEvent.setup()
        await rotate(user)
        Object.defineProperty(navigator, 'clipboard', {
            value: { writeText: vi.fn().mockRejectedValue(new Error('denied')) },
            configurable: true,
        })
        Object.defineProperty(document, 'execCommand', {
            value: vi.fn(() => {
                throw new Error('nope')
            }),
            configurable: true,
            writable: true,
        })

        await user.click(screen.getByRole('button', { name: 'Копировать' }))
        await waitFor(() =>
            expect(notifyError).toHaveBeenCalledWith('Не удалось скопировать токен — скопируйте вручную')
        )
        expect(notifySuccessSticky).not.toHaveBeenCalled()
    })

    it('shows rotating label while token rotation is pending', async () => {
        const user = userEvent.setup()
        vi.mocked(sensorsApi.get).mockResolvedValue(baseSensor)
        vi.mocked(sensorsApi.rotateToken).mockImplementationOnce(() => new Promise(() => {}))
        render(<SensorDetail />, { wrapper })
        await screen.findByText('Branch Sensor')
        await user.click(screen.getByRole('button', { name: 'Ротация токена' }))
        expect(await screen.findByRole('button', { name: 'Ротация...' })).toBeDisabled()
    })
})
