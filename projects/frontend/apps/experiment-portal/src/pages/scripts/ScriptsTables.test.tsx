import { describe, it, expect, vi } from 'vitest'
import { render, screen, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import ScriptsTable from './ScriptsTable'
import ExecutionsTable from './ExecutionsTable'
import type { Script, ScriptExecution } from '../../types/scripts'

vi.mock('../../components/PermissionGate', () => ({
  default: ({ children }: { children: React.ReactNode }) => <>{children}</>,
}))

const script = (over: Partial<Script> = {}): Script => ({
  id: 's1', name: 'Backup', description: null, target_service: 'experiment-service',
  script_type: 'bash', script_body: '', parameters_schema: {}, timeout_sec: 30,
  is_active: true, created_by: 'u', created_at: '', updated_at: '', ...over,
})

const execution = (over: Partial<ScriptExecution> = {}): ScriptExecution => ({
  id: 'e1', script_id: 's1', status: 'completed', parameters: {}, target_instance: null,
  requested_by: 'alice', started_at: '2026-01-01T10:00:00Z', finished_at: '2026-01-01T10:00:03Z',
  exit_code: 0, stdout: null, stderr: null, error_message: null, created_at: '', updated_at: '', ...over,
})

describe('ScriptsTable', () => {
  it('renders rows, placeholders and fires the row actions', async () => {
    const onEdit = vi.fn(), onToggle = vi.fn(), onDelete = vi.fn()
    const active = script()
    const inactive = script({ id: 's2', name: 'Old', description: 'legacy', is_active: false })
    render(
      <ScriptsTable scripts={[active, inactive]} toggleActivePending={false} deletePending={false}
        onEdit={onEdit} onToggleActive={onToggle} onDelete={onDelete} />,
    )
    const rows = screen.getAllByRole('row').slice(1)
    expect(within(rows[0]).getByText('—')).toBeInTheDocument()
    expect(within(rows[1]).getByText('legacy')).toBeInTheDocument()
    expect(within(rows[0]).getByText('Да')).toBeInTheDocument()
    expect(within(rows[1]).getByText('Нет')).toBeInTheDocument()

    await userEvent.click(within(rows[0]).getByText('Редактировать'))
    await userEvent.click(within(rows[0]).getByText('Деактивировать'))
    await userEvent.click(within(rows[1]).getByText('Активировать'))
    await userEvent.click(within(rows[1]).getByText('Удалить'))
    expect(onEdit).toHaveBeenCalledWith(active)
    expect(onToggle).toHaveBeenNthCalledWith(1, active)
    expect(onToggle).toHaveBeenNthCalledWith(2, inactive)
    expect(onDelete).toHaveBeenCalledWith(inactive)
  })

  it('disables the buttons while a mutation is pending', () => {
    render(
      <ScriptsTable scripts={[script()]} toggleActivePending deletePending
        onEdit={vi.fn()} onToggleActive={vi.fn()} onDelete={vi.fn()} />,
    )
    expect(screen.getByText('Деактивировать')).toBeDisabled()
    expect(screen.getByText('Удалить')).toBeDisabled()
    expect(screen.getByText('Редактировать')).toBeEnabled()
  })
})

describe('ExecutionsTable', () => {
  it('renders executions and opens a row by click, Enter and Space', async () => {
    const onRowClick = vi.fn()
    const ex = execution()
    render(<ExecutionsTable executions={[ex, execution({ id: 'e2', status: 'pending', started_at: null, finished_at: null, target_instance: 'node-1' })]}
      scriptName={() => 'Backup'} onRowClick={onRowClick} />)
    const rows = screen.getAllByRole('button')
    expect(screen.getByText('Завершён')).toBeInTheDocument()
    expect(screen.getByText('3.0 с')).toBeInTheDocument()
    expect(screen.getByText('node-1')).toBeInTheDocument()
    await userEvent.click(rows[0])
    rows[0].focus()
    await userEvent.keyboard('{Enter}')
    await userEvent.keyboard(' ')
    await userEvent.keyboard('x')
    expect(onRowClick).toHaveBeenCalledTimes(3)
    expect(onRowClick).toHaveBeenCalledWith(ex)
  })
})
