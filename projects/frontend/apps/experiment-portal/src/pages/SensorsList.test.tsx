import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import SensorsList from './SensorsList'
import { sensorsApi, projectsApi } from '../api/client'

// Мокаем API
vi.mock('../api/client', () => ({
    sensorsApi: {
        list: vi.fn(),
    },
    projectsApi: {
        list: vi.fn(),
    },
}))

const createWrapper = () => {
    const queryClient = new QueryClient({
        defaultOptions: {
            queries: { retry: false },
            mutations: { retry: false },
        },
    })
    return ({ children }: { children: React.ReactNode }) => (
        <QueryClientProvider client={queryClient}>
            <MemoryRouter>{children}</MemoryRouter>
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
    active_profile_id: null,
    calibration_notes: null,
    created_at: '2024-01-01T00:00:00Z',
    updated_at: '2024-01-01T00:00:00Z',
}

const mockProject = {
    id: 'project-1',
    name: 'Test Project',
    description: '',
    owner_id: 'user-1',
    created_at: '2024-01-01T00:00:00Z',
    updated_at: '2024-01-01T00:00:00Z',
}

describe('SensorsList', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        // Мокаем projectsApi.list по умолчанию
        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.list.mockResolvedValue({
            projects: [mockProject],
        })
    })

    it('renders loading state', async () => {
        const mockList = vi.mocked(sensorsApi.list)
        mockList.mockImplementation(
            () =>
                new Promise(() => {
                    // Never resolves to test loading state
                })
        )

        render(<SensorsList />, { wrapper: createWrapper() })

        // Ждем, пока проекты загрузятся и будет выбран первый проект
        await waitFor(() => {
            expect(screen.getByText(/загрузка/i)).toBeInTheDocument()
        })
    })

    it('renders error state', async () => {
        const mockList = vi.mocked(sensorsApi.list)
        mockList.mockRejectedValueOnce(new Error('Failed to load'))

        render(<SensorsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/ошибка загрузки датчиков/i)).toBeInTheDocument()
        }, { timeout: 3000 })
    })

    it('renders sensors list', async () => {
        const mockList = vi.mocked(sensorsApi.list)
        mockList.mockResolvedValueOnce({
            sensors: [mockSensor],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<SensorsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Temperature Sensor #1')).toBeInTheDocument()
        })

        // Проверяем статус в badge (используем getAllByText и фильтруем по классу)
        const allActiveTexts = screen.getAllByText(/активен/i)
        const badgeElement = allActiveTexts.find(
            (el) => el.classList.contains('badge')
        )
        expect(badgeElement).toBeInTheDocument()
        expect(badgeElement).toHaveClass('badge-success')
    })

    it('filters by project_id', async () => {
        const user = userEvent.setup()
        const mockList = vi.mocked(sensorsApi.list)
        const mockProjectsApi = vi.mocked(projectsApi)

        // Мокаем несколько проектов
        const project2 = {
            id: 'project-2',
            name: 'Project 2',
            description: '',
            owner_id: 'user-1',
            created_at: '2024-01-01T00:00:00Z',
            updated_at: '2024-01-01T00:00:00Z',
        }
        mockProjectsApi.list.mockResolvedValue({
            projects: [mockProject, project2],
        })

        mockList.mockResolvedValue({
            sensors: [],
            total: 0,
            page: 1,
            page_size: 20,
        })

        render(<SensorsList />, { wrapper: createWrapper() })

        // Ждем, пока проекты загрузятся и будет выбран первый проект
        await waitFor(() => {
            expect(mockList).toHaveBeenCalled()
        })

        // Ждем, пока форма загрузится
        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })

        const projectSelect = screen.getByLabelText(/проект/i)
        await user.selectOptions(projectSelect, 'project-2')

        // Ждем, пока будет вызов с project-2
        await waitFor(() => {
            const calls = mockList.mock.calls
            const lastCall = calls[calls.length - 1]
            expect(lastCall[0]).toMatchObject({
                project_id: 'project-2',
            })
        }, { timeout: 3000 })
    })

    it('filters by status', async () => {
        const user = userEvent.setup()
        const mockList = vi.mocked(sensorsApi.list)
        mockList.mockResolvedValue({
            sensors: [],
            total: 0,
            page: 1,
            page_size: 20,
        })

        render(<SensorsList />, { wrapper: createWrapper() })

        // Ждем, пока проекты загрузятся и будет выбран первый проект
        await waitFor(() => {
            expect(mockList).toHaveBeenCalled()
        })

        // Ждем, пока форма загрузится
        await waitFor(() => {
            expect(screen.getByLabelText(/статус/i)).toBeInTheDocument()
        })

        const statusSelect = screen.getByLabelText(/статус/i)
        await user.selectOptions(statusSelect, 'active')

        // Ждем debounce и новый вызов API
        await waitFor(() => {
            const calls = mockList.mock.calls
            const lastCall = calls[calls.length - 1]
            expect(lastCall[0]).toMatchObject({
                status: 'active',
            })
        }, { timeout: 3000 })
    })

    it('renders empty state when no sensors', async () => {
        const mockList = vi.mocked(sensorsApi.list)
        mockList.mockResolvedValueOnce({
            sensors: [],
            total: 0,
            page: 1,
            page_size: 20,
        })

        render(<SensorsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/датчики не найдены/i)).toBeInTheDocument()
        })
    })

    it('displays sensor information correctly', async () => {
        const mockList = vi.mocked(sensorsApi.list)
        mockList.mockResolvedValueOnce({
            sensors: [mockSensor],
            total: 1,
            page: 1,
            page_size: 20,
        })

        render(<SensorsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText('Temperature Sensor #1')).toBeInTheDocument()
        })

        // Проверяем единицы измерения (используем более специфичный поиск)
        const sensorCard = screen.getByText('Temperature Sensor #1').closest('.sensor-card')
        expect(sensorCard).toHaveTextContent('V → °C')

        // Проверяем тип датчика (исключаем заголовок)
        const typeElements = screen.getAllByText(/temperature/i)
        const typeInInfo = typeElements.find(
            (el) => el.closest('.sensor-info') !== null
        )
        expect(typeInInfo).toBeInTheDocument()
    })

    it('shows pagination when total exceeds page size', async () => {
        const mockList = vi.mocked(sensorsApi.list)
        mockList.mockResolvedValueOnce({
            sensors: [mockSensor],
            total: 25,
            page: 1,
            page_size: 20,
        })

        render(<SensorsList />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByText(/страница 1 из 2/i)).toBeInTheDocument()
        })
    })
})

