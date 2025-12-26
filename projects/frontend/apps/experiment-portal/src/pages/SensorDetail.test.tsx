import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import SensorDetail from './SensorDetail'
import { sensorsApi } from '../api/client'

// Мокаем sensorsApi
vi.mock('../api/client', () => ({
    sensorsApi: {
        get: vi.fn(),
        delete: vi.fn(),
        rotateToken: vi.fn(),
    },
}))

// Мокаем useNavigate и useParams
const mockNavigate = vi.fn()
vi.mock('react-router-dom', async () => {
    const actual = await vi.importActual('react-router-dom')
    return {
        ...actual,
        useNavigate: () => mockNavigate,
        useParams: () => ({ id: 'sensor-1' }),
    }
})

const createWrapper = () => {
    const queryClient = new QueryClient({
        defaultOptions: {
            queries: { retry: false },
            mutations: { retry: false },
        },
    })
    return ({ children }: { children: React.ReactNode }) => (
        <QueryClientProvider client={queryClient}>
            <MemoryRouter>
                {children}
            </MemoryRouter>
        </QueryClientProvider>
    )
}

const mockSensor = {
    id: 'sensor-1',
    project_id: 'project-1',
    name: 'Temperature Sensor #1',
    type: 'temperature',
    input_unit: 'V',
    display_unit: '°C',
    status: 'active' as const,
    token_preview: 'abcd',
    last_heartbeat: '2024-01-01T12:00:00Z',
    active_profile_id: 'profile-1',
    calibration_notes: 'Calibrated at 20°C',
    created_at: '2024-01-01T00:00:00Z',
    updated_at: '2024-01-01T00:00:00Z',
}

describe('SensorDetail', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockNavigate.mockClear()
    })

    it('renders loading state', () => {
        const mockGet = vi.mocked(sensorsApi.get)
        mockGet.mockImplementation(
            () =>
                new Promise(() => {
                    // Never resolves to test loading state
                })
        )

        render(<SensorDetail />, { wrapper: createWrapper() })

        expect(screen.getByText(/загрузка/i)).toBeInTheDocument()
    })

    it('renders error state', async () => {
        const mockGet = vi.mocked(sensorsApi.get)
        mockGet.mockRejectedValueOnce(new Error('Sensor not found'))

        render(<SensorDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/датчик не найден/i)).toBeInTheDocument()
        })
    })

    it('renders sensor information', async () => {
        const mockGet = vi.mocked(sensorsApi.get)
        mockGet.mockResolvedValueOnce(mockSensor)

        render(<SensorDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Temperature Sensor #1')).toBeInTheDocument()
        }, { timeout: 3000 })

        // Проверяем отдельные элементы
        const allActiveTexts = screen.getAllByText(/активен/i)
        const badgeElement = allActiveTexts.find(
            (el) => el.classList.contains('badge')
        )
        expect(badgeElement).toBeInTheDocument()

        const sensorHeader = screen.getByText('Temperature Sensor #1').closest('.sensor-header')
        expect(sensorHeader).toHaveTextContent('temperature')
        expect(sensorHeader).toHaveTextContent('V')
        expect(sensorHeader).toHaveTextContent('°C')
    })

    it('displays calibration notes when present', async () => {
        const mockGet = vi.mocked(sensorsApi.get)
        mockGet.mockResolvedValueOnce(mockSensor)

        render(<SensorDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Temperature Sensor #1')).toBeInTheDocument()
        }, { timeout: 3000 })

        await waitFor(() => {
            expect(screen.getByText(/заметки по калибровке/i)).toBeInTheDocument()
            expect(screen.getByText('Calibrated at 20°C')).toBeInTheDocument()
        }, { timeout: 3000 })
    })

    it('handles sensor deletion', async () => {
        const user = userEvent.setup()
        const mockGet = vi.mocked(sensorsApi.get)
        const mockDelete = vi.mocked(sensorsApi.delete)
        mockGet.mockResolvedValueOnce(mockSensor)
        mockDelete.mockResolvedValueOnce(undefined)

        // Мокаем window.confirm
        window.confirm = vi.fn(() => true)

        render(<SensorDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Temperature Sensor #1')).toBeInTheDocument()
        }, { timeout: 3000 })

        const deleteButton = screen.getByRole('button', { name: /удалить/i })
        await user.click(deleteButton)

        await waitFor(() => {
            expect(mockDelete).toHaveBeenCalled()
            expect(mockNavigate).toHaveBeenCalledWith('/sensors')
        })
    })

    it('does not delete when confirmation is cancelled', async () => {
        const user = userEvent.setup()
        const mockGet = vi.mocked(sensorsApi.get)
        const mockDelete = vi.mocked(sensorsApi.delete)
        mockGet.mockResolvedValueOnce(mockSensor)

        // Мокаем window.confirm для отмены
        window.confirm = vi.fn(() => false)

        render(<SensorDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Temperature Sensor #1')).toBeInTheDocument()
        }, { timeout: 3000 })

        const deleteButton = screen.getByRole('button', { name: /удалить/i })
        await user.click(deleteButton)

        expect(mockDelete).not.toHaveBeenCalled()
    })

    it('rotates token and displays new token', async () => {
        const user = userEvent.setup()
        const mockGet = vi.mocked(sensorsApi.get)
        const mockRotateToken = vi.mocked(sensorsApi.rotateToken)
        mockGet.mockResolvedValueOnce(mockSensor)
        mockRotateToken.mockResolvedValueOnce({
            sensor: mockSensor,
            token: 'new-token-67890',
        })

        render(<SensorDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Temperature Sensor #1')).toBeInTheDocument()
        }, { timeout: 3000 })

        const rotateButton = screen.getByRole('button', { name: /ротация токена/i })
        await user.click(rotateButton)

        await waitFor(() => {
            expect(mockRotateToken).toHaveBeenCalled()
            expect(screen.getByText('new-token-67890')).toBeInTheDocument()
        })
    })

    it('allows copying rotated token', async () => {
        const user = userEvent.setup()
        const mockGet = vi.mocked(sensorsApi.get)
        const mockRotateToken = vi.mocked(sensorsApi.rotateToken)
        mockGet.mockResolvedValueOnce(mockSensor)
        mockRotateToken.mockResolvedValueOnce({
            sensor: mockSensor,
            token: 'new-token-67890',
        })

        // Мокаем clipboard API
        const mockWriteText = vi.fn().mockResolvedValue(undefined)
        vi.stubGlobal('navigator', {
            clipboard: {
                writeText: mockWriteText,
            },
        })

        render(<SensorDetail />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Temperature Sensor #1')).toBeInTheDocument()
        }, { timeout: 3000 })

        const rotateButton = screen.getByRole('button', { name: /ротация токена/i })
        await user.click(rotateButton)

        await waitFor(() => {
            expect(screen.getByText('new-token-67890')).toBeInTheDocument()
        }, { timeout: 3000 })

        const copyButton = screen.getByRole('button', { name: /копировать/i })
        await user.click(copyButton)

        await waitFor(() => {
            expect(mockWriteText).toHaveBeenCalledWith('new-token-67890')
        })
    })
})

