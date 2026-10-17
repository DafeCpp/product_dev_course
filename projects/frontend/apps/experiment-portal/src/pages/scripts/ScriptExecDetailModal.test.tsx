import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ScriptExecDetailModal from './ScriptExecDetailModal'
import type { ScriptExecution } from '../../types/scripts'

vi.mock('../../api/scripts', () => ({ scriptsApi: { cancelExecution: vi.fn() } }))
import { scriptsApi } from '../../api/scripts'

const execution = (over: Partial<ScriptExecution> = {}): ScriptExecution => ({
  id: 'e1', script_id: 's1', status: 'running', parameters: { a: 1 }, target_instance: null,
  requested_by: 'alice', started_at: '2026-01-01T10:00:00Z', finished_at: null,
  exit_code: null, stdout: null, stderr: null, error_message: null, created_at: '', updated_at: '', ...over,
})

function renderModal(ex: ScriptExecution, handlers = { onClose: vi.fn(), onCancelled: vi.fn() }) {
  const qc = new QueryClient({ defaultOptions: { mutations: { retry: false } } })
  render(
    <QueryClientProvider client={qc}>
      <ScriptExecDetailModal execution={ex} scriptName="Backup" {...handlers} />
    </QueryClientProvider>,
  )
  return handlers
}

describe('ScriptExecDetailModal', () => {
  beforeEach(() => vi.clearAllMocks())

  it('shows details of a running execution and cancels it', async () => {
    vi.mocked(scriptsApi.cancelExecution).mockResolvedValue(undefined as never)
    const h = renderModal(execution())
    expect(screen.getByText('Backup')).toBeInTheDocument()
    expect(screen.getByText('Выполняется')).toBeInTheDocument()
    expect(screen.getByText(/"a": 1/)).toBeInTheDocument()
    expect(screen.queryByText('Stdout')).toBeNull()
    await userEvent.click(screen.getByText('Отменить выполнение'))
    await waitFor(() => expect(h.onCancelled).toHaveBeenCalled())
    expect(scriptsApi.cancelExecution).toHaveBeenCalledWith('e1')
  })

  it('keeps the modal open when cancelling fails', async () => {
    vi.mocked(scriptsApi.cancelExecution).mockRejectedValue(new Error('no'))
    const h = renderModal(execution({ status: 'pending' }))
    await userEvent.click(screen.getByText('Отменить выполнение'))
    await waitFor(() => expect(scriptsApi.cancelExecution).toHaveBeenCalled())
    expect(h.onCancelled).not.toHaveBeenCalled()
  })

  it('shows output sections and no cancel button for a finished execution', async () => {
    const h = renderModal(execution({
      status: 'failed', exit_code: 2, finished_at: '2026-01-01T10:00:05Z',
      stdout: 'out', stderr: 'err', error_message: 'broke',
    }))
    expect(screen.getByText('out')).toBeInTheDocument()
    expect(screen.getByText('err')).toBeInTheDocument()
    expect(screen.getByText('broke')).toBeInTheDocument()
    expect(screen.getByText('2')).toBeInTheDocument()
    expect(screen.queryByText('Отменить выполнение')).toBeNull()
    await userEvent.click(screen.getByText('Закрыть'))
    expect(h.onClose).toHaveBeenCalled()
  })
})
