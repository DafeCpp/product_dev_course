import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ScriptExecuteModal from './ScriptExecuteModal'
import type { Script } from '../../types/scripts'

vi.mock('../../api/scripts', () => ({ scriptsApi: { executeScript: vi.fn() } }))
import { scriptsApi } from '../../api/scripts'

const script = (over: Partial<Script> = {}): Script => ({
  id: 's1', name: 'Backup', description: null, target_service: 'experiment-service',
  script_type: 'bash', script_body: '', parameters_schema: {}, timeout_sec: 30,
  is_active: true, created_by: 'u', created_at: '', updated_at: '', ...over,
})
const withParams = script({ id: 's2', name: 'Param', parameters_schema: { x: { type: 'number' } } })

function renderModal() {
  const handlers = { onClose: vi.fn(), onExecuted: vi.fn() }
  const qc = new QueryClient({ defaultOptions: { mutations: { retry: false } } })
  render(
    <QueryClientProvider client={qc}>
      <ScriptExecuteModal scripts={[script(), withParams]} {...handlers} />
    </QueryClientProvider>,
  )
  return handlers
}

describe('ScriptExecuteModal', () => {
  beforeEach(() => vi.clearAllMocks())

  it('requires a script before moving on', async () => {
    renderModal()
    await userEvent.click(screen.getByText('Далее'))
    expect(screen.getByRole('alert')).toHaveTextContent('Выберите скрипт')
    await userEvent.selectOptions(screen.getByLabelText('Скрипт'), 's1')
    expect(screen.queryByRole('alert')).toBeNull()
  })

  it('runs a script without parameters through all three steps', async () => {
    vi.mocked(scriptsApi.executeScript).mockResolvedValue({} as never)
    const h = renderModal()
    await userEvent.selectOptions(screen.getByLabelText('Скрипт'), 's1')
    await userEvent.click(screen.getByText('Далее'))
    expect(screen.getByText('Нет параметров для этого скрипта.')).toBeInTheDocument()
    await userEvent.click(screen.getByText('Далее'))
    expect(screen.getByText('{}')).toBeInTheDocument()
    await userEvent.click(screen.getByText('Запустить'))
    await waitFor(() => expect(h.onExecuted).toHaveBeenCalled())
    expect(scriptsApi.executeScript).toHaveBeenCalledWith('s1', { parameters: {} })
  })

  it('validates JSON parameters and sends the parsed object', async () => {
    vi.mocked(scriptsApi.executeScript).mockResolvedValue({} as never)
    const h = renderModal()
    await userEvent.selectOptions(screen.getByLabelText('Скрипт'), 's2')
    await userEvent.click(screen.getByText('Далее'))

    const area = screen.getByLabelText('Параметры (JSON)')
    await userEvent.type(area, 'nope')
    await userEvent.click(screen.getByText('Далее'))
    expect(screen.getByRole('alert')).toHaveTextContent('Неверный формат JSON')

    await userEvent.clear(area)
    await userEvent.type(area, '[[1]')
    await userEvent.click(screen.getByText('Далее'))
    expect(screen.getByRole('alert')).toHaveTextContent('Должен быть JSON-объект')

    await userEvent.clear(area)
    await userEvent.click(screen.getByText('Далее')) // empty params are allowed
    await userEvent.click(screen.getByText('Назад'))
    await userEvent.type(screen.getByLabelText('Параметры (JSON)'), '{{"x": 5}')
    await userEvent.click(screen.getByText('Далее'))
    expect(screen.getByText(/"x": 5/)).toBeInTheDocument()
    await userEvent.click(screen.getByText('Запустить'))
    await waitFor(() => expect(h.onExecuted).toHaveBeenCalled())
    expect(scriptsApi.executeScript).toHaveBeenCalledWith('s2', { parameters: { x: 5 } })
  })

  it('does not call onExecuted when the request fails, and can go back or cancel', async () => {
    vi.mocked(scriptsApi.executeScript).mockRejectedValue(new Error('boom'))
    const h = renderModal()
    await userEvent.selectOptions(screen.getByLabelText('Скрипт'), 's1')
    await userEvent.click(screen.getByText('Далее'))
    await userEvent.click(screen.getByText('Далее'))
    await userEvent.click(screen.getByText('Запустить'))
    await waitFor(() => expect(scriptsApi.executeScript).toHaveBeenCalled())
    expect(h.onExecuted).not.toHaveBeenCalled()
    await userEvent.click(screen.getByText('Назад'))
    await userEvent.click(screen.getByText('Отмена'))
    expect(h.onClose).toHaveBeenCalled()
  })
})
