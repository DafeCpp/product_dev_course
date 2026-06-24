import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { MemoryRouter } from 'react-router-dom'
import Configs from './Configs'
import type { ConfigResponse } from '../types/configs'

// ---------------------------------------------------------------------------
// configsApi mock
// ---------------------------------------------------------------------------
vi.mock('../api/configs', () => ({
  configsApi: {
    listConfigs: vi.fn(),
    activate: vi.fn(),
    deactivate: vi.fn(),
    deleteConfig: vi.fn(),
    rollback: vi.fn(),
  },
}))

// usePermissions — driven by a mutable permission set per test
let granted = new Set<string>()
vi.mock('../hooks/usePermissions', () => ({
  usePermissions: () => ({
    hasSystemPermission: (p: string) => granted.has(p),
    hasPermission: (p: string) => granted.has(p),
    hasAnyPermission: (...p: string[]) => p.some((x) => granted.has(x)),
    isSuperadmin: false,
    systemPermissions: [...granted],
    projectPermissions: [],
    permissions: [...granted],
    isLoading: false,
  }),
}))

// Modal stubs
vi.mock('./configs/ConfigFormModal', () => ({
  default: ({ onClose }: { onClose: () => void }) => (
    <div data-testid="config-form-modal">
      <button onClick={onClose}>Close form</button>
    </div>
  ),
}))
vi.mock('./configs/ConfigHistoryModal', () => ({
  default: ({ onClose }: { onClose: () => void }) => (
    <div data-testid="config-history-modal">
      <button onClick={onClose}>Close history</button>
    </div>
  ),
}))

import { configsApi } from '../api/configs'

const CONFIG: ConfigResponse = {
  id: 'c1',
  service_name: 'auth-service',
  project_id: null,
  key: 'login_rate_limit',
  config_type: 'qos',
  description: null,
  value: { max: 10 },
  metadata: {},
  is_active: true,
  is_critical: false,
  is_sensitive: false,
  version: 3,
  created_by: 'u1',
  updated_by: 'u1',
  created_at: '2026-06-01T00:00:00Z',
  updated_at: '2026-06-01T00:00:00Z',
}

function renderPage() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } })
  return render(
    <QueryClientProvider client={qc}>
      <MemoryRouter>
        <Configs />
      </MemoryRouter>
    </QueryClientProvider>,
  )
}

describe('Configs page', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    granted = new Set()
    ;(configsApi.listConfigs as ReturnType<typeof vi.fn>).mockResolvedValue({
      items: [CONFIG],
      next_cursor: null,
    })
  })

  it('shows "no access" without configs.view', async () => {
    renderPage()
    expect(await screen.findByText('Нет доступа')).toBeInTheDocument()
  })

  it('renders the config row for a viewer', async () => {
    granted = new Set(['configs.view'])
    renderPage()
    expect(await screen.findByText('login_rate_limit')).toBeInTheDocument()
    expect(screen.getByText('auth-service')).toBeInTheDocument()
    expect(screen.getByText('v3')).toBeInTheDocument()
  })

  it('viewer sees only История — no write/publish actions', async () => {
    granted = new Set(['configs.view'])
    renderPage()
    await screen.findByText('login_rate_limit')
    expect(screen.getByRole('button', { name: 'История' })).toBeInTheDocument()
    expect(screen.queryByRole('button', { name: '+ Создать' })).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: 'Редактировать' })).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: 'Деактивировать' })).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: 'Удалить' })).not.toBeInTheDocument()
  })

  it('editor sees create/edit/delete but NOT activate/rollback (4-eyes)', async () => {
    granted = new Set(['configs.view', 'configs.create', 'configs.update', 'configs.delete'])
    renderPage()
    await screen.findByText('login_rate_limit')
    expect(screen.getByRole('button', { name: '+ Создать' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Редактировать' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Удалить' })).toBeInTheDocument()
    expect(screen.queryByRole('button', { name: 'Деактивировать' })).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: 'Откат' })).not.toBeInTheDocument()
  })

  it('operator sees activate/rollback but NOT edit/delete/create (4-eyes)', async () => {
    granted = new Set(['configs.view', 'configs.activate', 'configs.rollback'])
    renderPage()
    await screen.findByText('login_rate_limit')
    expect(screen.getByRole('button', { name: 'Деактивировать' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Откат' })).toBeInTheDocument()
    expect(screen.queryByRole('button', { name: '+ Создать' })).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: 'Редактировать' })).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: 'Удалить' })).not.toBeInTheDocument()
  })

  it('opens the create modal on "+ Создать"', async () => {
    granted = new Set(['configs.view', 'configs.create'])
    renderPage()
    await screen.findByText('login_rate_limit')
    await userEvent.click(screen.getByRole('button', { name: '+ Создать' }))
    expect(await screen.findByTestId('config-form-modal')).toBeInTheDocument()
  })

  it('opens the history modal on "История"', async () => {
    granted = new Set(['configs.view'])
    renderPage()
    await screen.findByText('login_rate_limit')
    await userEvent.click(screen.getByRole('button', { name: 'История' }))
    expect(await screen.findByTestId('config-history-modal')).toBeInTheDocument()
  })
})
