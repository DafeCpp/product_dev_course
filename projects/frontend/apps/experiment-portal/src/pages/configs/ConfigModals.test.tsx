import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ConfigFormModal from './ConfigFormModal'
import ConfigHistoryModal from './ConfigHistoryModal'
import type { ConfigResponse, ConfigHistoryItem } from '../../types/configs'

vi.mock('../../api/configs', () => ({
  configsApi: {
    createConfig: vi.fn(), patchConfig: vi.fn(), dryRunCreate: vi.fn(), dryRunPatch: vi.fn(), getHistory: vi.fn(),
  },
}))
import { configsApi } from '../../api/configs'

const existing = {
  id: 'c1', service_name: 'auth', key: 'rate', config_type: 'qos', description: 'old', value: { a: 1 },
  is_critical: true, is_sensitive: false, version: 3,
} as unknown as ConfigResponse

function wrap(ui: React.ReactNode) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } })
  return render(<QueryClientProvider client={qc}>{ui}</QueryClientProvider>)
}

describe('ConfigFormModal', () => {
  const handlers = { onClose: vi.fn(), onSaved: vi.fn() }
  beforeEach(() => vi.clearAllMocks())

  it('validates required fields and JSON in create mode', async () => {
    wrap(<ConfigFormModal config={null} {...handlers} />)
    await userEvent.click(screen.getByText('Создать'))
    expect(screen.getAllByText('Обязательное поле')).toHaveLength(2)
    await userEvent.type(screen.getByLabelText('Сервис *'), 'svc')
    await userEvent.type(screen.getByLabelText('Ключ *'), 'k')
    const value = screen.getByLabelText('Значение (JSON)')
    await userEvent.clear(value)
    await userEvent.type(value, 'nope')
    await userEvent.click(screen.getByText('Создать'))
    expect(screen.getByText('Невалидный JSON')).toBeInTheDocument()
    expect(configsApi.createConfig).not.toHaveBeenCalled()
  })

  it('creates a config with a trimmed payload', async () => {
    vi.mocked(configsApi.createConfig).mockResolvedValue({} as never)
    wrap(<ConfigFormModal config={null} {...handlers} />)
    await userEvent.type(screen.getByLabelText('Сервис *'), ' svc ')
    await userEvent.type(screen.getByLabelText('Ключ *'), ' k ')
    await userEvent.selectOptions(screen.getByLabelText('Тип'), 'qos')
    await userEvent.type(screen.getByLabelText('Описание'), ' d ')
    await userEvent.click(screen.getByLabelText('Критичный'))
    await userEvent.click(screen.getByLabelText('Sensitive'))
    await userEvent.click(screen.getByText('Создать'))
    await waitFor(() => expect(handlers.onSaved).toHaveBeenCalled())
    expect(configsApi.createConfig).toHaveBeenCalledWith({
      service_name: 'svc', key: 'k', config_type: 'qos', description: 'd',
      value: { enabled: true }, is_critical: true, is_sensitive: true,
    })
  })

  it('shows a dry-run preview and hides it after the next edit', async () => {
    vi.mocked(configsApi.dryRunCreate).mockResolvedValue({ preview: { ok: 1 }, dry_run: true })
    wrap(<ConfigFormModal config={null} {...handlers} />)
    await userEvent.type(screen.getByLabelText('Сервис *'), 'svc')
    await userEvent.type(screen.getByLabelText('Ключ *'), 'k')
    await userEvent.click(screen.getByText('Dry-run'))
    expect(await screen.findByTestId('dry-run-preview')).toHaveTextContent('"ok": 1')
    await userEvent.type(screen.getByLabelText('Описание'), 'x')
    expect(screen.queryByTestId('dry-run-preview')).toBeNull()
  })

  it('edits with the optimistic-lock version and a reason', async () => {
    vi.mocked(configsApi.patchConfig).mockResolvedValue({} as never)
    vi.mocked(configsApi.dryRunPatch).mockResolvedValue({ preview: {}, dry_run: true })
    wrap(<ConfigFormModal config={existing} {...handlers} />)
    expect(screen.getByText('Редактировать «rate» (v3)')).toBeInTheDocument()
    expect(screen.queryByLabelText('Сервис *')).toBeNull()
    expect(screen.getByLabelText('Критичный')).toBeChecked()
    await userEvent.type(screen.getByLabelText('Причина изменения'), 'tune')
    await userEvent.click(screen.getByText('Dry-run'))
    await waitFor(() => expect(configsApi.dryRunPatch).toHaveBeenCalledWith('c1', expect.objectContaining({ version: 3, change_reason: 'tune' })))
    await userEvent.click(screen.getByText('Сохранить'))
    await waitFor(() => expect(handlers.onSaved).toHaveBeenCalled())
    expect(configsApi.patchConfig).toHaveBeenCalledWith('c1', {
      version: 3, value: { a: 1 }, description: 'old', is_critical: true, is_sensitive: false, change_reason: 'tune',
    })
  })

  it('keeps the modal open when saving or dry-run fails, and cancels', async () => {
    vi.mocked(configsApi.createConfig).mockRejectedValue(new Error('x'))
    vi.mocked(configsApi.dryRunCreate).mockRejectedValue(new Error('y'))
    wrap(<ConfigFormModal config={null} {...handlers} />)
    await userEvent.type(screen.getByLabelText('Сервис *'), 'svc')
    await userEvent.type(screen.getByLabelText('Ключ *'), 'k')
    await userEvent.click(screen.getByText('Dry-run'))
    await waitFor(() => expect(configsApi.dryRunCreate).toHaveBeenCalled())
    await userEvent.click(screen.getByText('Создать'))
    await waitFor(() => expect(configsApi.createConfig).toHaveBeenCalled())
    expect(handlers.onSaved).not.toHaveBeenCalled()
    expect(screen.queryByTestId('dry-run-preview')).toBeNull()
    await userEvent.click(screen.getByText('Отмена'))
    expect(handlers.onClose).toHaveBeenCalled()
  })
})

describe('ConfigHistoryModal', () => {
  beforeEach(() => vi.clearAllMocks())
  const item = (over: Partial<ConfigHistoryItem>): ConfigHistoryItem => ({
    id: 'h1', config_id: 'c1', version: 2, value: { z: 1 }, is_active: true, changed_by: 'bob',
    change_reason: 'why', changed_at: '2026-01-01T00:00:00Z', ...over,
  } as ConfigHistoryItem)

  it('lists versions with reasons and the active marker', async () => {
    vi.mocked(configsApi.getHistory).mockResolvedValue({ items: [item({}), item({ id: 'h2', version: 1, is_active: false, change_reason: null })] })
    wrap(<ConfigHistoryModal config={existing} onClose={vi.fn()} />)
    expect(screen.getByText('Загрузка истории...')).toBeInTheDocument()
    expect(await screen.findByText('v2')).toBeInTheDocument()
    expect(screen.getByText('v1')).toBeInTheDocument()
    expect(screen.getByText('why')).toBeInTheDocument()
    expect(screen.getAllByText(/· active/)).toHaveLength(1)
    expect(screen.getByText('История «rate»')).toBeInTheDocument()
  })

  it('shows the empty state', async () => {
    vi.mocked(configsApi.getHistory).mockResolvedValue({ items: [] })
    wrap(<ConfigHistoryModal config={existing} onClose={vi.fn()} />)
    expect(await screen.findByText('История пуста')).toBeInTheDocument()
  })

  it('shows an error when loading fails', async () => {
    vi.mocked(configsApi.getHistory).mockRejectedValue(new Error('boom'))
    wrap(<ConfigHistoryModal config={existing} onClose={vi.fn()} />)
    expect(await screen.findByText('Ошибка загрузки истории')).toBeInTheDocument()
  })
})
