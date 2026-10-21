import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { MemoryRouter } from 'react-router-dom'
import Configs from './Configs'
import type { ConfigResponse } from '../types/configs'
import { pickMaterialSelectOption } from '../testUtils/materialSelect'

vi.mock('../api/configs', () => ({
  configsApi: {
    listConfigs: vi.fn(),
    activate: vi.fn(),
    deactivate: vi.fn(),
    deleteConfig: vi.fn(),
    rollback: vi.fn(),
  },
}))

let granted = new Set<string>()
let permsLoading = false
vi.mock('../hooks/usePermissions', () => ({
  usePermissions: () => ({
    hasSystemPermission: (p: string) => granted.has(p),
    hasPermission: (p: string) => granted.has(p),
    hasAnyPermission: (...p: string[]) => p.some((x) => granted.has(x)),
    isSuperadmin: false,
    systemPermissions: [...granted],
    projectPermissions: [],
    permissions: [...granted],
    isLoading: permsLoading,
  }),
}))

vi.mock('./configs/ConfigFormModal', () => ({
  default: ({
    config,
    onClose,
    onSaved,
  }: {
    config: ConfigResponse | null
    onClose: () => void
    onSaved: () => void
  }) => (
    <div data-testid="config-form-modal">
      <span>{config ? `edit:${config.key}` : 'create'}</span>
      <button onClick={onClose}>Close form</button>
      <button onClick={onSaved}>Save form</button>
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

const api = configsApi as unknown as Record<string, ReturnType<typeof vi.fn>>

const BASE: ConfigResponse = {
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

const INACTIVE: ConfigResponse = {
  ...BASE,
  id: 'c2',
  key: 'flag_x',
  config_type: 'feature_flag',
  is_active: false,
  is_critical: true,
  is_sensitive: true,
  version: 1,
}

const ALL = [
  'configs.view',
  'configs.create',
  'configs.update',
  'configs.delete',
  'configs.activate',
  'configs.rollback',
]

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

describe('Configs page — branches', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    granted = new Set(ALL)
    permsLoading = false
    api.listConfigs.mockResolvedValue({ items: [BASE, INACTIVE], next_cursor: null })
    api.activate.mockResolvedValue({ ...INACTIVE, is_active: true })
    api.deactivate.mockResolvedValue({ ...BASE, is_active: false })
    api.deleteConfig.mockResolvedValue(undefined)
    api.rollback.mockResolvedValue(BASE)
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('shows permissions loading state', () => {
    permsLoading = true
    renderPage()
    expect(screen.getByText('Проверка прав доступа...')).toBeInTheDocument()
  })

  it('shows loading then error state', async () => {
    api.listConfigs.mockRejectedValue(new Error('boom'))
    renderPage()
    expect(screen.getByText('Загрузка конфигов...')).toBeInTheDocument()
    expect(await screen.findByText(/boom|Ошибка загрузки конфигов/)).toBeInTheDocument()
  })

  it('shows empty state', async () => {
    api.listConfigs.mockResolvedValue({ items: [], next_cursor: null })
    renderPage()
    expect(await screen.findByText('Конфигов не найдено')).toBeInTheDocument()
  })

  it('shows badges, inactive status and hides rollback for v1', async () => {
    renderPage()
    await screen.findByText('flag_x')
    expect(screen.getByText('sensitive')).toBeInTheDocument()
    expect(screen.getByText('critical')).toBeInTheDocument()
    expect(screen.getByText('inactive')).toBeInTheDocument()
    expect(screen.getByText('active')).toBeInTheDocument()
    expect(screen.getAllByRole('button', { name: 'Откат' })).toHaveLength(1)
    expect(screen.getByRole('button', { name: 'Активировать' })).toBeInTheDocument()
  })

  it('applies service, type and active filters', async () => {
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('flag_x')
    await user.type(screen.getByLabelText('Фильтр по сервису'), 'a')
    await waitFor(() =>
      expect(api.listConfigs).toHaveBeenLastCalledWith(
        expect.objectContaining({ service: 'a' }),
      ),
    )
    await pickMaterialSelectOption(user, 'Тип', 'qos')
    await waitFor(() =>
      expect(api.listConfigs).toHaveBeenLastCalledWith(
        expect.objectContaining({ config_type: 'qos' }),
      ),
    )
    const sw = screen.getByRole('switch')
    await user.click(sw)
    await waitFor(() =>
      expect(api.listConfigs).toHaveBeenLastCalledWith(
        expect.objectContaining({ is_active: true }),
      ),
    )
    expect(screen.getByText('Активные')).toBeInTheDocument()
    await user.click(screen.getByRole('switch'))
    expect(screen.getByText('Все')).toBeInTheDocument()
  })

  it('activates and deactivates configs', async () => {
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('flag_x')
    await user.click(screen.getByRole('button', { name: 'Деактивировать' }))
    await waitFor(() =>
      expect(api.deactivate).toHaveBeenCalledWith('c1', {
        version: 3,
        changeReason: 'deactivated via portal',
      }),
    )
    await user.click(screen.getByRole('button', { name: 'Активировать' }))
    await waitFor(() =>
      expect(api.activate).toHaveBeenCalledWith('c2', {
        version: 1,
        changeReason: 'activated via portal',
      }),
    )
  })

  it('toggle error path does not crash', async () => {
    api.deactivate.mockRejectedValue(new Error('conflict'))
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('flag_x')
    await user.click(screen.getByRole('button', { name: 'Деактивировать' }))
    await waitFor(() => expect(api.deactivate).toHaveBeenCalled())
    expect(screen.getByText('login_rate_limit')).toBeInTheDocument()
  })

  it('delete: confirm cancelled vs accepted', async () => {
    const confirmSpy = vi.spyOn(window, 'confirm').mockReturnValueOnce(false).mockReturnValueOnce(true)
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('flag_x')
    const del = screen.getAllByRole('button', { name: 'Удалить' })[0]
    await user.click(del)
    expect(api.deleteConfig).not.toHaveBeenCalled()
    await user.click(del)
    await waitFor(() =>
      expect(api.deleteConfig).toHaveBeenCalledWith('c1', {
        version: 3,
        changeReason: 'deleted via portal',
      }),
    )
    expect(confirmSpy).toHaveBeenCalledTimes(2)
  })

  it('rollback: cancel, invalid input and valid target', async () => {
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('flag_x')
    const btn = screen.getByRole('button', { name: 'Откат' })

    await user.click(btn)
    expect(screen.getByRole('heading', { name: 'Откат «login_rate_limit» (текущая v3)' })).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'Отмена' }))
    expect(screen.queryByRole('heading', { name: /Откат «login_rate_limit»/ })).not.toBeInTheDocument()

    await user.click(btn)
    const input = screen.getByLabelText('Целевая версия')
    for (const bad of ['', '0', '3']) {
      await user.clear(input)
      if (bad) await user.type(input, bad)
      await user.click(screen.getByRole('button', { name: 'Откатить' }))
      expect(screen.getByRole('alert')).toHaveTextContent('Укажите версию от 1 до 2')
    }
    expect(api.rollback).not.toHaveBeenCalled()

    await user.clear(input)
    await user.type(input, '2{Enter}')
    await waitFor(() =>
      expect(api.rollback).toHaveBeenCalledWith('c1', {
        version: 3,
        targetVersion: 2,
        changeReason: 'rollback to v2 via portal',
      }),
    )
    expect(screen.queryByLabelText('Целевая версия')).not.toBeInTheDocument()
  })

  it('opens edit modal, closes it and saves', async () => {
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('flag_x')
    await user.click(screen.getAllByRole('button', { name: 'Редактировать' })[0])
    expect(screen.getByText('edit:login_rate_limit')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'Close form' }))
    expect(screen.queryByTestId('config-form-modal')).not.toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: '+ Создать' }))
    expect(screen.getByText('create')).toBeInTheDocument()
    const calls = api.listConfigs.mock.calls.length
    await user.click(screen.getByRole('button', { name: 'Save form' }))
    expect(screen.queryByTestId('config-form-modal')).not.toBeInTheDocument()
    await waitFor(() => expect(api.listConfigs.mock.calls.length).toBeGreaterThan(calls))
  })

  it('closes history modal', async () => {
    const user = userEvent.setup()
    renderPage()
    await screen.findByText('flag_x')
    await user.click(screen.getAllByRole('button', { name: 'История' })[0])
    await user.click(screen.getByRole('button', { name: 'Close history' }))
    expect(screen.queryByTestId('config-history-modal')).not.toBeInTheDocument()
  })
})
