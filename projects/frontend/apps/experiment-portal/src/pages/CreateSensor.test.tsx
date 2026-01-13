import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import CreateSensor from './CreateSensor'
import { sensorsApi, projectsApi } from '../api/client'

// Мокаем API
vi.mock('../api/client', () => ({
    sensorsApi: {
        create: vi.fn(),
        addProject: vi.fn(),
    },
    projectsApi: {
        list: vi.fn(),
    },
}))

// Мокаем useNavigate
const mockNavigate = vi.fn()
vi.mock('react-router-dom', async () => {
    const actual = await vi.importActual('react-router-dom')
    return {
        ...actual,
        useNavigate: () => mockNavigate,
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
            <MemoryRouter>{children}</MemoryRouter>
        </QueryClientProvider>
    )
}

describe('CreateSensor', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        mockNavigate.mockClear()
        // Мокаем projectsApi.list по умолчанию
        const mockProjectsApi = vi.mocked(projectsApi)
        mockProjectsApi.list.mockResolvedValue({
            projects: [
                {
                    id: 'project-1',
                    name: 'Test Project',
                    description: '',
                    owner_id: 'user-1',
                    created_at: '2024-01-01T00:00:00Z',
                    updated_at: '2024-01-01T00:00:00Z',
                },
                {
                    id: 'project-2',
                    name: 'Second Project',
                    description: '',
                    owner_id: 'user-1',
                    created_at: '2024-01-01T00:00:00Z',
                    updated_at: '2024-01-01T00:00:00Z',
                },
            ],
        })
    })

    it('renders form with all fields', async () => {
        render(<CreateSensor />, { wrapper: createWrapper() })

        expect(screen.getByRole('heading', { name: /зарегистрировать датчик/i })).toBeInTheDocument()
        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        expect(screen.getByLabelText(/название/i)).toBeInTheDocument()
        expect(screen.getByLabelText(/тип датчика/i)).toBeInTheDocument()
        expect(screen.getByLabelText(/входная единица измерения/i)).toBeInTheDocument()
        expect(screen.getByLabelText(/единица отображения/i)).toBeInTheDocument()
    })

    it('validates required fields', async () => {
        const user = userEvent.setup()
        render(<CreateSensor />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })

        const submitButton = screen.getByRole('button', { name: /зарегистрировать/i })
        await user.click(submitButton)

        // HTML5 validation should prevent submission
        const projectSelect = screen.getByLabelText(/проект/i) as HTMLSelectElement
        expect(projectSelect.validity.valueMissing).toBe(true)
    })

    it('submits form with correct data', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(sensorsApi.create)
        const mockAddProject = vi.mocked(sensorsApi.addProject)
        const mockResponse = {
            sensor: {
                id: 'sensor-1',
                project_id: 'project-1',
                name: 'Test Sensor',
                type: 'temperature',
                input_unit: 'V',
                display_unit: '°C',
                status: 'registering' as const,
                created_at: '2024-01-01T00:00:00Z',
                updated_at: '2024-01-01T00:00:00Z',
            },
            token: 'test-token-12345',
        }
        mockCreate.mockResolvedValueOnce(mockResponse)

        render(<CreateSensor />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        await user.selectOptions(screen.getByLabelText(/проект/i), ['project-1'])
        await user.type(screen.getByLabelText(/название/i), 'Test Sensor')
        await user.selectOptions(screen.getByLabelText(/тип датчика/i), 'temperature')
        await user.type(screen.getByLabelText(/входная единица измерения/i), 'V')
        await user.type(screen.getByLabelText(/единица отображения/i), '°C')

        const submitButton = screen.getByRole('button', { name: /зарегистрировать/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(mockCreate).toHaveBeenCalledWith({
                project_id: 'project-1',
                name: 'Test Sensor',
                type: 'temperature',
                input_unit: 'V',
                display_unit: '°C',
                calibration_notes: undefined,
            })
        })
        expect(mockAddProject).not.toHaveBeenCalled()
    })

    it('adds sensor to additional projects after creation', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(sensorsApi.create)
        const mockAddProject = vi.mocked(sensorsApi.addProject)

        mockCreate.mockResolvedValueOnce({
            sensor: {
                id: 'sensor-1',
                project_id: 'project-1',
                name: 'Test Sensor',
                type: 'temperature',
                input_unit: 'V',
                display_unit: '°C',
                status: 'registering' as const,
                created_at: '2024-01-01T00:00:00Z',
                updated_at: '2024-01-01T00:00:00Z',
            },
            token: 'test-token-12345',
        })
        mockAddProject.mockResolvedValueOnce(undefined)

        render(<CreateSensor />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        // Выбираем два проекта (первый будет основным)
        await user.selectOptions(screen.getByLabelText(/проект/i), ['project-1', 'project-2'])
        await user.type(screen.getByLabelText(/название/i), 'Test Sensor')
        await user.selectOptions(screen.getByLabelText(/тип датчика/i), 'temperature')
        await user.type(screen.getByLabelText(/входная единица измерения/i), 'V')
        await user.type(screen.getByLabelText(/единица отображения/i), '°C')

        await user.click(screen.getByRole('button', { name: /зарегистрировать/i }))

        await waitFor(() => {
            expect(mockCreate).toHaveBeenCalledWith(
                expect.objectContaining({
                    project_id: 'project-1',
                })
            )
        })

        await waitFor(() => {
            expect(mockAddProject).toHaveBeenCalledWith('sensor-1', 'project-2')
        })
    })

    it('shows error on submission failure', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(sensorsApi.create)
        mockCreate.mockRejectedValueOnce({
            response: {
                data: { error: 'Sensor name already exists' },
            },
        })

        render(<CreateSensor />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        await user.selectOptions(screen.getByLabelText(/проект/i), ['project-1'])
        await user.type(screen.getByLabelText(/название/i), 'Test Sensor')
        await user.selectOptions(screen.getByLabelText(/тип датчика/i), 'temperature')
        await user.type(screen.getByLabelText(/входная единица измерения/i), 'V')
        await user.type(screen.getByLabelText(/единица отображения/i), '°C')

        const submitButton = screen.getByRole('button', { name: /зарегистрировать/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(screen.getByText(/sensor name already exists/i)).toBeInTheDocument()
        })
    })

    it('displays token after successful registration', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(sensorsApi.create)
        const mockResponse = {
            sensor: {
                id: 'sensor-1',
                project_id: 'project-1',
                name: 'Test Sensor',
                type: 'temperature',
                input_unit: 'V',
                display_unit: '°C',
                status: 'registering' as const,
                created_at: '2024-01-01T00:00:00Z',
                updated_at: '2024-01-01T00:00:00Z',
            },
            token: 'test-token-12345',
        }
        mockCreate.mockResolvedValueOnce(mockResponse)

        render(<CreateSensor />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        await user.selectOptions(screen.getByLabelText(/проект/i), ['project-1'])
        await user.type(screen.getByLabelText(/название/i), 'Test Sensor')
        await user.selectOptions(screen.getByLabelText(/тип датчика/i), 'temperature')
        await user.type(screen.getByLabelText(/входная единица измерения/i), 'V')
        await user.type(screen.getByLabelText(/единица отображения/i), '°C')

        const submitButton = screen.getByRole('button', { name: /зарегистрировать/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(screen.getByText(/датчик успешно зарегистрирован/i)).toBeInTheDocument()
            expect(screen.getByText('test-token-12345')).toBeInTheDocument()
        })
    })

    it('allows copying token to clipboard', async () => {
        const user = userEvent.setup()
        const mockCreate = vi.mocked(sensorsApi.create)
        const mockResponse = {
            sensor: {
                id: 'sensor-1',
                project_id: 'project-1',
                name: 'Test Sensor',
                type: 'temperature',
                input_unit: 'V',
                display_unit: '°C',
                status: 'registering' as const,
                created_at: '2024-01-01T00:00:00Z',
                updated_at: '2024-01-01T00:00:00Z',
            },
            token: 'test-token-12345',
        }
        mockCreate.mockResolvedValueOnce(mockResponse)

        // Мокаем clipboard API
        const mockWriteText = vi.fn().mockResolvedValue(undefined)
        vi.stubGlobal('navigator', {
            clipboard: {
                writeText: mockWriteText,
            },
        })

        render(<CreateSensor />, { wrapper: createWrapper() })

        await waitFor(() => {
            expect(screen.getByLabelText(/проект/i)).toBeInTheDocument()
        })
        await user.selectOptions(screen.getByLabelText(/проект/i), ['project-1'])
        await user.type(screen.getByLabelText(/название/i), 'Test Sensor')
        await user.selectOptions(screen.getByLabelText(/тип датчика/i), 'temperature')
        await user.type(screen.getByLabelText(/входная единица измерения/i), 'V')
        await user.type(screen.getByLabelText(/единица отображения/i), '°C')

        const submitButton = screen.getByRole('button', { name: /зарегистрировать/i })
        await user.click(submitButton)

        await waitFor(() => {
            expect(screen.getByText('test-token-12345')).toBeInTheDocument()
        })

        const copyButton = screen.getByRole('button', { name: /копировать/i })
        await user.click(copyButton)

        expect(mockWriteText).toHaveBeenCalledWith('test-token-12345')
    })
})

