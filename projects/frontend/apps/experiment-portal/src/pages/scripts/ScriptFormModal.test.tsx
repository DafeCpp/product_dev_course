import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ScriptFormModal from './ScriptFormModal'
import type { Script } from '../../types/scripts'

vi.mock('../../api/scripts', () => ({ scriptsApi: { createScript: vi.fn(), updateScript: vi.fn() } }))
import { scriptsApi } from '../../api/scripts'

const existing: Script = {
  id: 's1', name: 'Backup', description: 'daily', target_service: 'experiment-service',
  script_type: 'bash', script_body: 'echo 1', parameters_schema: { a: { type: 'string' } },
  timeout_sec: 60, is_active: true, created_by: 'u', created_at: '', updated_at: '',
}

function renderModal(script: Script | null) {
  const handlers = { onClose: vi.fn(), onSaved: vi.fn() }
  const qc = new QueryClient({ defaultOptions: { mutations: { retry: false } } })
  render(
    <QueryClientProvider client={qc}>
      <ScriptFormModal script={script} {...handlers} />
    </QueryClientProvider>,
  )
  return handlers
}

describe('ScriptFormModal', () => {
  beforeEach(() => vi.clearAllMocks())

  it('validates required fields and clears an error on edit', async () => {
    const h = renderModal(null)
    await userEvent.click(screen.getByText('Создать'))
    expect(screen.getAllByText('Обязательное поле')).toHaveLength(2)
    expect(scriptsApi.createScript).not.toHaveBeenCalled()
    await userEvent.type(screen.getByLabelText('Название *'), 'N')
    expect(screen.getAllByText('Обязательное поле')).toHaveLength(1)
    expect(h.onSaved).not.toHaveBeenCalled()
  })

  it('rejects bad parameter schemas', async () => {
    renderModal(null)
    await userEvent.type(screen.getByLabelText('Название *'), 'N')
    await userEvent.type(screen.getByLabelText('Сервис *'), 'svc')
    const schema = screen.getByLabelText('Схема параметров (JSON-объект)')
    await userEvent.type(schema, 'bad')
    await userEvent.click(screen.getByText('Создать'))
    expect(screen.getByText('Невалидный JSON')).toBeInTheDocument()
    await userEvent.clear(schema)
    await userEvent.type(schema, '[[1]')
    await userEvent.click(screen.getByText('Создать'))
    expect(screen.getByText('Должен быть JSON-объект')).toBeInTheDocument()
    expect(scriptsApi.createScript).not.toHaveBeenCalled()
  })

  it('creates a script with a trimmed payload', async () => {
    vi.mocked(scriptsApi.createScript).mockResolvedValue(existing)
    const h = renderModal(null)
    await userEvent.type(screen.getByLabelText('Название *'), '  N  ')
    await userEvent.type(screen.getByLabelText('Сервис *'), 'svc')
    await userEvent.selectOptions(screen.getByLabelText('Тип'), 'javascript')
    await userEvent.clear(screen.getByLabelText('Таймаут (сек)'))
    await userEvent.type(screen.getByLabelText('Таймаут (сек)'), '45')
    await userEvent.type(screen.getByLabelText('Тело скрипта'), 'x')
    await userEvent.click(screen.getByText('Создать'))
    await waitFor(() => expect(h.onSaved).toHaveBeenCalled())
    expect(scriptsApi.createScript).toHaveBeenCalledWith({
      name: 'N', description: undefined, target_service: 'svc', script_type: 'javascript',
      script_body: 'x', timeout_sec: 45, parameters_schema: {},
    })
  })

  it('edits an existing script', async () => {
    vi.mocked(scriptsApi.updateScript).mockResolvedValue(existing)
    const h = renderModal(existing)
    expect(screen.getByLabelText('Название *')).toHaveValue('Backup')
    expect(screen.getByLabelText('Схема параметров (JSON-объект)')).toHaveValue(JSON.stringify(existing.parameters_schema, null, 2))
    await userEvent.type(screen.getByLabelText('Описание'), '!')
    await userEvent.click(screen.getByText('Сохранить'))
    await waitFor(() => expect(h.onSaved).toHaveBeenCalled())
    expect(scriptsApi.updateScript).toHaveBeenCalledWith('s1', expect.objectContaining({ description: 'daily!', parameters_schema: existing.parameters_schema }))
  })

  it('stays open when saving fails and closes on cancel', async () => {
    vi.mocked(scriptsApi.createScript).mockRejectedValue(new Error('x'))
    const h = renderModal(null)
    await userEvent.type(screen.getByLabelText('Название *'), 'N')
    await userEvent.type(screen.getByLabelText('Сервис *'), 'svc')
    await userEvent.click(screen.getByText('Создать'))
    await waitFor(() => expect(scriptsApi.createScript).toHaveBeenCalled())
    expect(h.onSaved).not.toHaveBeenCalled()
    await userEvent.click(screen.getByText('Отмена'))
    expect(h.onClose).toHaveBeenCalled()
  })
})
