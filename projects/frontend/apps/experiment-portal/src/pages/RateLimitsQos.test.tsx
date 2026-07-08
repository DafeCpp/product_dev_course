import { describe, it, expect, beforeEach, vi } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { MemoryRouter } from 'react-router-dom'
import RateLimitsQos from './RateLimitsQos'

// Mock the API module
const mockListConfigs = vi.fn()
const mockCreateConfig = vi.fn()
const mockPatchConfig = vi.fn()
const mockDryRunCreate = vi.fn()
const mockDryRunPatch = vi.fn()

vi.mock('../api/configs', () => ({
  configsApi: {
    listConfigs: () => mockListConfigs(),
    createConfig: (data: unknown) => mockCreateConfig(data),
    patchConfig: (id: string, data: unknown) => mockPatchConfig(id, data),
    dryRunCreate: (data: unknown) => mockDryRunCreate(data),
    dryRunPatch: (id: string, data: unknown) => mockDryRunPatch(id, data),
  },
}))

// Mock usePermissions
const mockHasPermission = vi.fn()
vi.mock('../hooks/usePermissions', () => ({
  usePermissions: () => ({
    hasSystemPermission: (perm: string) => mockHasPermission(perm),
    isLoading: false,
    user: null,
  }),
}))

// Mock notifications
vi.mock('../utils/notify', () => ({
  notifySuccess: vi.fn(),
  notifyError: vi.fn(),
}))

function renderPage(initialEntries: string[] = ['/admin/rate-limits']) {
  const queryClient = new QueryClient({
    defaultOptions: {
      queries: { retry: false },
      mutations: { retry: false },
    },
  })

  return render(
    <QueryClientProvider client={queryClient}>
      <MemoryRouter initialEntries={initialEntries}>
        <RateLimitsQos />
      </MemoryRouter>
    </QueryClientProvider>,
  )
}

describe('RateLimitsQos', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    mockHasPermission.mockReturnValue(true)
    mockListConfigs.mockResolvedValue({
      items: [],
      next_cursor: null,
    })
  })

  it('shows "Нет доступа" when user lacks configs.view', async () => {
    mockHasPermission.mockImplementation((perm) => perm !== 'configs.view')

    renderPage()

    await waitFor(() => {
      expect(screen.getByText('Нет доступа')).toBeInTheDocument()
    })
  })

  it('renders page title and main heading', async () => {
    renderPage()

    await waitFor(() => {
      expect(screen.getByText('Rate Limits & QoS конфигурация')).toBeInTheDocument()
    })
  })

  it('renders three service selector buttons', async () => {
    renderPage()

    await waitFor(() => {
      expect(screen.getByText('Auth Service')).toBeInTheDocument()
      expect(screen.getByText('Experiment Service')).toBeInTheDocument()
      expect(screen.getByText('Telemetry Ingest')).toBeInTheDocument()
    })
  })

  it('shows Auth QoS form by default', async () => {
    renderPage()

    await waitFor(() => {
      expect(screen.getByText(/Auth Service — TTL конфигурация/)).toBeInTheDocument()
    })
  })

  it('shows loading state while fetching configs', async () => {
    mockListConfigs.mockImplementation(
      () => new Promise(() => {}) /* never resolves */,
    )

    renderPage()

    expect(screen.getByText('Загрузка конфигов...')).toBeInTheDocument()
  })

  it('shows error state on api failure', async () => {
    const testError = new Error('Network error')
    mockListConfigs.mockRejectedValue(testError)

    renderPage()

    await waitFor(() => {
      expect(screen.getByText(/Network error/)).toBeInTheDocument()
    })
  })
})
