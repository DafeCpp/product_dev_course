import { describe, it, expect, vi, beforeEach } from 'vitest'
import React from 'react'
import { MemoryRouter } from 'react-router-dom'
import { render, screen, waitFor } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'

// Мокаем API перед импортом компонента
const mockList = vi.fn()
vi.mock('../api/client', () => ({
  sensorsApi: {
    list: mockList,
    get: vi.fn(),
    create: vi.fn(),
    update: vi.fn(),
    rotateToken: vi.fn(),
  },
  experimentsApi: {},
  runsApi: {},
}))

import SensorsList from './SensorsList'

describe('SensorsList', () => {
  let queryClient: QueryClient

  beforeEach(() => {
    queryClient = new QueryClient({
      defaultOptions: {
        queries: {
          retry: false,
        },
      },
    })
    vi.clearAllMocks()
    mockList.mockClear()
  })

  const renderWithProviders = (component: React.ReactElement) => {
    return render(
      <QueryClientProvider client={queryClient}>
        <MemoryRouter>{component}</MemoryRouter>
      </QueryClientProvider>
    )
  }

  it('отображает загрузку при получении данных', () => {
    mockList.mockImplementation(
      () =>
        new Promise(() => {
          // Никогда не резолвим промис, чтобы показать состояние загрузки
        })
    )

    renderWithProviders(<SensorsList />)

    expect(screen.getByText('Загрузка...')).toBeInTheDocument()
  })

  it('отображает ошибку при неудачной загрузке', async () => {
    mockList.mockRejectedValue(new Error('Ошибка сети'))

    renderWithProviders(<SensorsList />)

    await waitFor(() => {
      expect(screen.getByText('Ошибка загрузки датчиков')).toBeInTheDocument()
    })
  })

  it('отображает список датчиков', async () => {
    const mockSensors = {
      sensors: [
        {
          id: '1',
          project_id: 'project-1',
          name: 'Температурный датчик',
          type: 'temperature',
          input_unit: 'V',
          display_unit: '°C',
          status: 'active',
          last_heartbeat: new Date(Date.now() - 5000).toISOString(), // 5 секунд назад
          created_at: new Date().toISOString(),
          updated_at: new Date().toISOString(),
        },
        {
          id: '2',
          project_id: 'project-1',
          name: 'Датчик давления',
          type: 'pressure',
          input_unit: 'V',
          display_unit: 'Pa',
          status: 'inactive',
          created_at: new Date().toISOString(),
          updated_at: new Date().toISOString(),
        },
      ],
      total: 2,
      page: 1,
      page_size: 20,
    }

    mockList.mockResolvedValue(mockSensors)

    renderWithProviders(<SensorsList />)

    await waitFor(() => {
      expect(screen.getByText('Температурный датчик')).toBeInTheDocument()
      expect(screen.getByText('Датчик давления')).toBeInTheDocument()
    })

    expect(screen.getByText('Активен')).toBeInTheDocument()
    expect(screen.getByText('Неактивен')).toBeInTheDocument()
  })

  it('отображает статус онлайн для активного датчика с недавним heartbeat', async () => {
    const mockSensors = {
      sensors: [
        {
          id: '1',
          project_id: 'project-1',
          name: 'Активный датчик',
          type: 'temperature',
          input_unit: 'V',
          display_unit: '°C',
          status: 'active',
          last_heartbeat: new Date(Date.now() - 5000).toISOString(), // 5 секунд назад
          created_at: new Date().toISOString(),
          updated_at: new Date().toISOString(),
        },
      ],
      total: 1,
      page: 1,
      page_size: 20,
    }

    mockList.mockResolvedValue(mockSensors)

    renderWithProviders(<SensorsList />)

    await waitFor(() => {
      expect(screen.getByText('Онлайн')).toBeInTheDocument()
    })
  })

  it('отображает статус офлайн для датчика без heartbeat', async () => {
    const mockSensors = {
      sensors: [
        {
          id: '1',
          project_id: 'project-1',
          name: 'Неактивный датчик',
          type: 'temperature',
          input_unit: 'V',
          display_unit: '°C',
          status: 'inactive',
          created_at: new Date().toISOString(),
          updated_at: new Date().toISOString(),
        },
      ],
      total: 1,
      page: 1,
      page_size: 20,
    }

    mockList.mockResolvedValue(mockSensors)

    renderWithProviders(<SensorsList />)

    await waitFor(() => {
      expect(screen.getByText('Офлайн')).toBeInTheDocument()
    })
  })

  it('отображает пустое состояние когда датчиков нет', async () => {
    const mockSensors = {
      sensors: [],
      total: 0,
      page: 1,
      page_size: 20,
    }

    mockList.mockResolvedValue(mockSensors)

    renderWithProviders(<SensorsList />)

    await waitFor(() => {
      expect(screen.getByText('Датчики не найдены')).toBeInTheDocument()
    })
  })

  it('отображает пагинацию когда датчиков больше чем page_size', async () => {
    const mockSensors = {
      sensors: Array.from({ length: 20 }, (_, i) => ({
        id: `${i}`,
        project_id: 'project-1',
        name: `Датчик ${i}`,
        type: 'temperature',
        input_unit: 'V',
        display_unit: '°C',
        status: 'active',
        created_at: new Date().toISOString(),
        updated_at: new Date().toISOString(),
      })),
      total: 25,
      page: 1,
      page_size: 20,
    }

    mockList.mockResolvedValue(mockSensors)

    renderWithProviders(<SensorsList />)

    await waitFor(() => {
      expect(screen.getByText('Страница 1 из 2')).toBeInTheDocument()
    })
  })

  it('отображает фильтр по project_id', async () => {
    const mockSensors = {
      sensors: [],
      total: 0,
      page: 1,
      page_size: 20,
    }

    mockList.mockResolvedValue(mockSensors)

    renderWithProviders(<SensorsList />)

    await waitFor(() => {
      const projectInput = screen.getByPlaceholderText('UUID проекта')
      expect(projectInput).toBeInTheDocument()
    })
  })
})
