import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor, act } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ExperimentEditModal from './ExperimentEditModal'
import { experimentsApi } from '../api/client'
import { pickMaterialSelectOption } from '../testUtils/materialSelect'

vi.mock('../api/client', () => ({
  experimentsApi: { update: vi.fn(), archive: vi.fn() },
}))
vi.mock('../utils/notify', () => ({
  notifyError: vi.fn(), notifySuccess: vi.fn(), notifyInfo: vi.fn(), notifyWarning: vi.fn(),
}))

function exp(over: Record<string, unknown> = {}) {
  return {
    id: 'e1', project_id: 'p1', name: 'Wing test', description: 'desc', experiment_type: 'baseline',
    tags: ['a', 'b'], metadata: { k: 1 }, status: 'draft', ...over,
  }
}

function renderModal(experiment = exp(), onClose = vi.fn(), isOpen = true) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } })
  const utils = render(
    <QueryClientProvider client={qc}>
      <ExperimentEditModal isOpen={isOpen} onClose={onClose} experiment={experiment as never} />
    </QueryClientProvider>
  )
  return { ...utils, onClose }
}

describe('ExperimentEditModal', () => {
  beforeEach(() => vi.clearAllMocks())

  it('prefills the form from the experiment', () => {
    renderModal()
    expect(screen.getByLabelText(/Название/)).toHaveValue('Wing test')
    expect(screen.getByLabelText('Описание')).toHaveValue('desc')
    expect(screen.getByLabelText('Теги')).toHaveValue('a, b')
    expect((screen.getByLabelText(/Метаданные/) as HTMLTextAreaElement).value).toContain('"k": 1')
    expect(screen.getByText('p1')).toBeInTheDocument()
  })

  it('falls back to empty values for missing fields', () => {
    renderModal(exp({ description: null, experiment_type: null, tags: null, metadata: null }) as never)
    expect(screen.getByLabelText('Описание')).toHaveValue('')
    expect(screen.getByLabelText('Теги')).toHaveValue('')
    expect(screen.getByLabelText(/Метаданные/)).toHaveValue('{}')
  })

  it('sends a trimmed payload without status when it is unchanged', async () => {
    const user = userEvent.setup()
    vi.mocked(experimentsApi.update).mockResolvedValue({} as never)
    const { onClose } = renderModal()
    const name = screen.getByLabelText(/Название/)
    await user.clear(name)
    await user.type(name, '  New name  ')
    await user.clear(screen.getByLabelText('Описание'))
    await user.clear(screen.getByLabelText('Теги'))
    await user.type(screen.getByLabelText('Теги'), ' x , ,y ')
    await user.click(screen.getByRole('button', { name: 'Сохранить' }))
    await waitFor(() => expect(experimentsApi.update).toHaveBeenCalled())
    expect(vi.mocked(experimentsApi.update).mock.calls[0]).toEqual([
      'e1',
      { name: 'New name', description: undefined, experiment_type: 'baseline', tags: ['x', 'y'], metadata: { k: 1 } },
    ])
    await waitFor(() => expect(onClose).toHaveBeenCalled())
  })

  it('changes the experiment type', async () => {
    const user = userEvent.setup()
    vi.mocked(experimentsApi.update).mockResolvedValue({} as never)
    renderModal()
    await pickMaterialSelectOption(user, 'Тип эксперимента', 'Калибровка')
    await user.click(screen.getByRole('button', { name: 'Сохранить' }))
    await waitFor(() => expect(experimentsApi.update).toHaveBeenCalled())
    expect(vi.mocked(experimentsApi.update).mock.calls[0][1]).toMatchObject({ experiment_type: 'calibration' })
  })

  it('sends the new status for a regular transition', async () => {
    const user = userEvent.setup()
    vi.mocked(experimentsApi.update).mockResolvedValue({} as never)
    renderModal()
    await pickMaterialSelectOption(user, 'Статус', /running|Запущ|Выполня/i)
    await user.click(screen.getByRole('button', { name: 'Сохранить' }))
    await waitFor(() => expect(experimentsApi.update).toHaveBeenCalled())
    expect(vi.mocked(experimentsApi.update).mock.calls[0][1]).toMatchObject({ status: 'running' })
  })

  it('archives instead of updating when archived is selected', async () => {
    const user = userEvent.setup()
    vi.mocked(experimentsApi.archive).mockResolvedValue({} as never)
    const { onClose } = renderModal()
    await pickMaterialSelectOption(user, 'Статус', /archived|Архив/i)
    await user.click(screen.getByRole('button', { name: 'Сохранить' }))
    await waitFor(() => expect(experimentsApi.archive).toHaveBeenCalledWith('e1', { project_id: 'p1' }))
    expect(experimentsApi.update).not.toHaveBeenCalled()
    await waitFor(() => expect(onClose).toHaveBeenCalled())
  })

  it('disables the status select when there are no transitions', () => {
    renderModal(exp({ status: 'archived' }) as never)
    expect(screen.getByText('Для текущего статуса переходы недоступны.')).toBeInTheDocument()
    expect(screen.getByLabelText('Статус')).toBeDisabled()
  })

  it('shows available transitions hint for an active status', () => {
    renderModal(exp({ status: 'running' }) as never)
    expect(screen.getByText(/Доступные переходы зависят/)).toBeInTheDocument()
  })

  it('rejects invalid JSON metadata', async () => {
    const user = userEvent.setup()
    renderModal()
    const meta = screen.getByLabelText(/Метаданные/)
    await user.clear(meta)
    await user.type(meta, 'nope')
    await user.click(screen.getByRole('button', { name: 'Сохранить' }))
    expect(await screen.findByRole('alert')).toHaveTextContent('Неверный формат JSON в метаданных')
    expect(experimentsApi.update).not.toHaveBeenCalled()
  })

  it('treats empty and non-object metadata as an empty object', async () => {
    const user = userEvent.setup()
    vi.mocked(experimentsApi.update).mockResolvedValue({} as never)
    renderModal()
    const meta = screen.getByLabelText(/Метаданные/)
    await user.clear(meta)
    await user.click(screen.getByRole('button', { name: 'Сохранить' }))
    await waitFor(() => expect(experimentsApi.update).toHaveBeenCalledTimes(1))
    expect(vi.mocked(experimentsApi.update).mock.calls[0][1]).toMatchObject({ metadata: {} })
  })

  it('treats null metadata JSON as an empty object', async () => {
    const user = userEvent.setup()
    vi.mocked(experimentsApi.update).mockResolvedValue({} as never)
    renderModal()
    const meta = screen.getByLabelText(/Метаданные/)
    await user.clear(meta)
    await user.type(meta, 'null')
    await user.click(screen.getByRole('button', { name: 'Сохранить' }))
    await waitFor(() => expect(experimentsApi.update).toHaveBeenCalled())
    expect(vi.mocked(experimentsApi.update).mock.calls[0][1]).toMatchObject({ metadata: {} })
  })

  it('disables Save when the name is blank, and blocks the submit handler', async () => {
    const user = userEvent.setup()
    renderModal()
    const name = screen.getByLabelText(/Название/)
    await user.clear(name)
    await user.type(name, '   ')
    expect(screen.getByRole('button', { name: 'Сохранить' })).toBeDisabled()
    // pressing Enter in the field submits the form despite the disabled button
    await user.type(name, '{Enter}')
    expect(experimentsApi.update).not.toHaveBeenCalled()
  })

  it('shows the server error inline and stays open', async () => {
    const user = userEvent.setup()
    vi.mocked(experimentsApi.update).mockRejectedValue(new Error('conflict'))
    const { onClose } = renderModal()
    await user.click(screen.getByRole('button', { name: 'Сохранить' }))
    expect(await screen.findByRole('alert')).toBeInTheDocument()
    expect(onClose).not.toHaveBeenCalled()
  })

  it('does not close while saving, and closes on cancel otherwise', async () => {
    const user = userEvent.setup()
    let release: (v: unknown) => void = () => {}
    vi.mocked(experimentsApi.update).mockReturnValue(new Promise((r) => { release = r }) as never)
    const { onClose } = renderModal()
    await user.click(screen.getByRole('button', { name: 'Сохранить' }))
    const busy = await screen.findByRole('button', { name: 'Сохранение...' })
    expect(busy).toBeDisabled()
    expect(screen.getByRole('button', { name: 'Отмена' })).toBeDisabled()
    await act(async () => { release({}) })
    await waitFor(() => expect(onClose).toHaveBeenCalledTimes(1))
  })

  it('closes via Cancel when idle', async () => {
    const user = userEvent.setup()
    const { onClose } = renderModal()
    await user.click(screen.getByRole('button', { name: 'Отмена' }))
    expect(onClose).toHaveBeenCalled()
  })

  it('renders nothing while closed', () => {
    renderModal(exp(), vi.fn(), false)
    expect(screen.queryByText('Эксперимент: редактирование')).not.toBeInTheDocument()
  })
})
