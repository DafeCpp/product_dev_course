import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { formatDateTime, formatDateTimeSeconds } from '../utils/format'
import { render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { MemoryRouter, Route, Routes, useLocation } from 'react-router-dom'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import SensorDetailModal from './SensorDetailModal'
import { sensorsApi, projectsApi } from '../api/client'
import { notifyError, notifySuccess, notifySuccessSticky } from '../utils/notify'
import { pickMaterialSelectOption } from '../testUtils/materialSelect'

vi.mock('../api/client', () => ({
  sensorsApi: {
    get: vi.fn(), getProjects: vi.fn(), delete: vi.fn(), rotateToken: vi.fn(),
    addProject: vi.fn(), removeProject: vi.fn(), decideProjectRequest: vi.fn(),
  },
  projectsApi: { list: vi.fn() },
}))
vi.mock('../utils/notify', () => ({
  notifyError: vi.fn(), notifySuccess: vi.fn(), notifySuccessSticky: vi.fn(), notifyInfo: vi.fn(), notifyWarning: vi.fn(),
}))
vi.mock('./TestTelemetryModal', () => ({
  default: ({ isOpen, sensorToken }: { isOpen: boolean; sensorToken: string | null }) =>
    isOpen ? <div data-testid="test-telemetry">token={String(sensorToken)}</div> : null,
}))
vi.mock('./TelemetryStreamModal', () => ({
  default: ({ isOpen }: { isOpen: boolean }) => (isOpen ? <div data-testid="stream-modal" /> : null),
}))

function sensor(over: Record<string, unknown> = {}) {
  return {
    id: 's1', project_id: 'p1', name: 'Thermo', type: 'temperature', input_unit: 'mV', display_unit: 'C',
    status: 'active', token_preview: 'abcd', last_heartbeat: null, active_profile_id: 'prof-1',
    calibration_notes: 'calibrated yesterday', created_at: '2026-01-02T10:00:00Z', updated_at: '2026-01-03T11:30:00Z',
    ...over,
  }
}

const projects = [
  { id: 'p1', name: 'Main' },
  { id: 'p2', name: 'Second' },
  { id: 'p3', name: 'Third' },
]

function Where() {
  return <div data-testid="loc">{useLocation().pathname}</div>
}

function renderModal(onClose = vi.fn(), isOpen = true) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } })
  const utils = render(
    <QueryClientProvider client={qc}>
      <MemoryRouter initialEntries={['/']}>
        <Routes>
          <Route path="/" element={<SensorDetailModal isOpen={isOpen} onClose={onClose} sensorId="s1" />} />
          <Route path="*" element={<Where />} />
        </Routes>
      </MemoryRouter>
    </QueryClientProvider>
  )
  return { ...utils, onClose }
}

describe('SensorDetailModal', () => {
  let confirmSpy: ReturnType<typeof vi.spyOn>
  const realClipboard = Object.getOwnPropertyDescriptor(navigator, 'clipboard')

  beforeEach(() => {
    vi.clearAllMocks()
    confirmSpy = vi.spyOn(window, 'confirm').mockReturnValue(true)
    vi.mocked(sensorsApi.get).mockResolvedValue(sensor() as never)
    vi.mocked(sensorsApi.getProjects).mockResolvedValue({ project_ids: ['p1', 'p2'] })
    vi.mocked(projectsApi.list).mockResolvedValue({ projects } as never)
  })
  afterEach(() => {
    confirmSpy.mockRestore()
    if (realClipboard) Object.defineProperty(navigator, 'clipboard', realClipboard)
  })

  it('does not fetch while closed', () => {
    renderModal(vi.fn(), false)
    expect(sensorsApi.get).not.toHaveBeenCalled()
    expect(projectsApi.list).not.toHaveBeenCalled()
  })

  it('shows sensor info, calibration notes and the project table', async () => {
    renderModal()
    expect(await screen.findByText('calibrated yesterday')).toBeInTheDocument()
    expect(screen.getByText('Thermo')).toBeInTheDocument()
    expect(screen.getByText('temperature')).toBeInTheDocument()
    expect(screen.getByText('****abcd')).toBeInTheDocument()
    expect(screen.getByText('prof-1')).toBeInTheDocument()
    expect(screen.getByText('Никогда')).toBeInTheDocument()
    expect(screen.getByText(formatDateTime('2026-01-02T10:00:00Z'))).toBeInTheDocument()
    expect(screen.getByText(formatDateTime('2026-01-03T11:30:00Z'))).toBeInTheDocument()
    const table = await screen.findByRole('table')
    expect(within(table).getByText('Main')).toBeInTheDocument()
    expect(within(table).getByText('Second')).toBeInTheDocument()
    expect(within(table).getByText('Основной')).toBeInTheDocument()
    expect(within(table).getByText('Нельзя удалить основной проект')).toBeInTheDocument()
  })

  it('omits optional blocks when the sensor lacks them', async () => {
    vi.mocked(sensorsApi.get).mockResolvedValue(sensor({ token_preview: null, active_profile_id: null, calibration_notes: null }) as never)
    vi.mocked(sensorsApi.getProjects).mockResolvedValue({ project_ids: [] })
    renderModal()
    expect(await screen.findByText('Датчик не привязан ни к одному проекту')).toBeInTheDocument()
    expect(screen.queryByText(/Токен \(превью\)/)).not.toBeInTheDocument()
    expect(screen.queryByText(/Активный профиль/)).not.toBeInTheDocument()
    expect(screen.queryByText('Заметки по калибровке')).not.toBeInTheDocument()
  })

  it('labels unknown projects', async () => {
    vi.mocked(sensorsApi.getProjects).mockResolvedValue({ project_ids: ['p1', 'ghost'] })
    renderModal()
    expect(await screen.findByText('Неизвестный проект')).toBeInTheDocument()
  })

  it.each([
    [30 * 1000, 'Только что'],
    [5 * 60 * 1000 + 5000, '5 мин назад'],
    [3 * 3600 * 1000 + 5000, '3 ч назад'],
  ])('formats a heartbeat %i ms ago as %s', async (ago, expected) => {
    vi.mocked(sensorsApi.get).mockResolvedValue(sensor({ last_heartbeat: new Date(Date.now() - ago).toISOString() }) as never)
    renderModal()
    expect(await screen.findByText(expected)).toBeInTheDocument()
  })

  it('formats an old heartbeat as a date', async () => {
    const old = new Date(Date.now() - 3 * 86400 * 1000)
    vi.mocked(sensorsApi.get).mockResolvedValue(sensor({ last_heartbeat: old.toISOString() }) as never)
    renderModal()
    expect(await screen.findByText(formatDateTimeSeconds(old))).toBeInTheDocument()
  })

  it('shows a loading indicator, then an error with the message', async () => {
    vi.mocked(sensorsApi.get).mockRejectedValue(new Error('sensor gone'))
    renderModal()
    expect(await screen.findByText(/sensor gone/)).toBeInTheDocument()
    expect(screen.queryByText('Профили и настройки →')).not.toBeInTheDocument()
  })

  it('navigates to the sensor page and closes', async () => {
    const user = userEvent.setup()
    const { onClose } = renderModal()
    await user.click(await screen.findByRole('button', { name: 'Профили и настройки →' }))
    expect(onClose).toHaveBeenCalled()
    expect(screen.getByTestId('loc')).toHaveTextContent('/sensors/s1')
  })

  it('opens the test telemetry and live stream modals', async () => {
    const user = userEvent.setup()
    renderModal()
    await user.click(await screen.findByRole('button', { name: 'Тестовая отправка' }))
    expect(screen.getByTestId('test-telemetry')).toHaveTextContent('token=null')
    await user.click(screen.getByRole('button', { name: 'Live telemetry' }))
    expect(screen.getByTestId('stream-modal')).toBeInTheDocument()
  })

  describe('delete', () => {
    it('does nothing when the confirmation is declined', async () => {
      const user = userEvent.setup()
      confirmSpy.mockReturnValue(false)
      renderModal()
      const btns = await screen.findAllByRole('button', { name: 'Удалить' })
      await user.click(btns[0])
      expect(sensorsApi.delete).not.toHaveBeenCalled()
    })

    it('deletes the sensor and closes', async () => {
      const user = userEvent.setup()
      vi.mocked(sensorsApi.delete).mockResolvedValue(undefined)
      const { onClose } = renderModal()
      const btns = await screen.findAllByRole('button', { name: 'Удалить' })
      await user.click(btns[0])
      await waitFor(() => expect(sensorsApi.delete).toHaveBeenCalledWith('s1'))
      await waitFor(() => expect(onClose).toHaveBeenCalled())
    })
  })

  describe('token rotation', () => {
    beforeEach(() => {
      vi.mocked(sensorsApi.rotateToken).mockResolvedValue({ token: 'tok-123' } as never)
    })

    async function rotate(user: ReturnType<typeof userEvent.setup>) {
      await user.click(await screen.findByRole('button', { name: 'Ротация токена' }))
      expect(await screen.findByText('tok-123')).toBeInTheDocument()
    }

    it('shows the new token, passes it to the test modal and can dismiss it', async () => {
      const user = userEvent.setup()
      renderModal()
      await rotate(user)
      await user.click(screen.getByRole('button', { name: 'Тестовая отправка' }))
      expect(screen.getByTestId('test-telemetry')).toHaveTextContent('token=tok-123')
      await user.click(screen.getByRole('button', { name: 'Закрыть' }))
      expect(screen.queryByText('tok-123')).not.toBeInTheDocument()
    })

    it('copies the token through the clipboard', async () => {
      const user = userEvent.setup()
      const writeText = vi.fn().mockResolvedValue(undefined)
      renderModal()
      await rotate(user)
      Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true })
      await user.click(screen.getAllByRole('button', { name: 'Копировать' })[0])
      await waitFor(() => expect(writeText).toHaveBeenCalledWith('tok-123'))
      expect(notifySuccessSticky).toHaveBeenCalled()
    })

    it('falls back to execCommand when the clipboard rejects', async () => {
      const user = userEvent.setup()
      renderModal()
      await rotate(user)
      Object.defineProperty(navigator, 'clipboard', { value: { writeText: vi.fn().mockRejectedValue(new Error('x')) }, configurable: true })
      const exec = vi.fn(() => true)
      Object.defineProperty(document, 'execCommand', { value: exec, configurable: true })
      await user.click(screen.getAllByRole('button', { name: 'Копировать' })[0])
      await waitFor(() => expect(exec).toHaveBeenCalledWith('copy'))
      expect(notifySuccessSticky).toHaveBeenCalled()
    })

    it('falls back to execCommand without a clipboard, and reports total failure', async () => {
      const user = userEvent.setup()
      renderModal()
      await rotate(user)
      Object.defineProperty(navigator, 'clipboard', { value: undefined, configurable: true })
      Object.defineProperty(document, 'execCommand', { value: vi.fn(() => { throw new Error('y') }), configurable: true })
      await user.click(screen.getAllByRole('button', { name: 'Копировать' })[0])
      await waitFor(() => expect(notifyError).toHaveBeenCalledWith('Не удалось скопировать токен — скопируйте вручную'))
    })

    it('reports a rotation failure without showing a token', async () => {
      const user = userEvent.setup()
      vi.mocked(sensorsApi.rotateToken).mockRejectedValue(new Error('no'))
      renderModal()
      await user.click(await screen.findByRole('button', { name: 'Ротация токена' }))
      await waitFor(() => expect(sensorsApi.rotateToken).toHaveBeenCalled())
      expect(screen.queryByText(/Сохраните новый токен/)).not.toBeInTheDocument()
    })
  })

  describe('sensor id copy', () => {
    it('copies via the clipboard and shows feedback', async () => {
      const user = userEvent.setup()
      renderModal()
      const btn = await screen.findByRole('button', { name: 'Копировать ID датчика' })
      const writeText = vi.fn().mockResolvedValue(undefined)
      Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true })
      await user.click(btn)
      await waitFor(() => expect(writeText).toHaveBeenCalledWith('s1'))
      expect(await screen.findByText('Скопировано')).toBeInTheDocument()
      expect(notifySuccess).toHaveBeenCalledWith('ID датчика в буфере обмена', 'Скопировано')
      await waitFor(() => expect(screen.queryByText('Скопировано')).not.toBeInTheDocument(), { timeout: 3000 })
    })

    it('falls back to execCommand and reports a failure', async () => {
      const user = userEvent.setup()
      renderModal()
      const btn = await screen.findByRole('button', { name: 'Копировать ID датчика' })
      Object.defineProperty(navigator, 'clipboard', { value: { writeText: vi.fn().mockRejectedValue(new Error('x')) }, configurable: true })
      const exec = vi.fn(() => true)
      Object.defineProperty(document, 'execCommand', { value: exec, configurable: true })
      await user.click(btn)
      await waitFor(() => expect(exec).toHaveBeenCalledWith('copy'))
      expect(notifySuccess).toHaveBeenCalled()

      vi.mocked(notifySuccess).mockClear()
      Object.defineProperty(document, 'execCommand', { value: vi.fn(() => { throw new Error('y') }), configurable: true })
      await user.click(btn)
      await waitFor(() => expect(notifyError).toHaveBeenCalledWith('Не удалось скопировать ID датчика'))
      expect(notifySuccess).not.toHaveBeenCalled()
    })
  })

  describe('projects', () => {
    it('removes a non-primary project after confirmation', async () => {
      const user = userEvent.setup()
      vi.mocked(sensorsApi.removeProject).mockResolvedValue(undefined)
      renderModal()
      const table = await screen.findByRole('table')
      await user.click(within(table).getByRole('button', { name: 'Удалить' }))
      expect(confirmSpy).toHaveBeenCalledWith('Удалить датчик из проекта Second?')
      await waitFor(() => expect(sensorsApi.removeProject).toHaveBeenCalledWith('s1', 'p2'))
    })

    it('keeps the project when removal is declined', async () => {
      const user = userEvent.setup()
      confirmSpy.mockReturnValue(false)
      renderModal()
      const table = await screen.findByRole('table')
      await user.click(within(table).getByRole('button', { name: 'Удалить' }))
      expect(sensorsApi.removeProject).not.toHaveBeenCalled()
    })

    it('adds a project from the remaining ones and approves it from the primary project', async () => {
      const user = userEvent.setup()
      vi.mocked(sensorsApi.addProject).mockResolvedValue({ id: 'r1' } as never)
      vi.mocked(sensorsApi.decideProjectRequest).mockResolvedValue({ id: 'r1', status: 'approved' } as never)
      renderModal()
      await screen.findByRole('table')
      await user.click(screen.getByRole('button', { name: 'Добавить проект' }))
      expect(screen.getByRole('button', { name: 'Добавить' })).toBeDisabled()
      await pickMaterialSelectOption(user, /^Проект/, 'Third')
      await user.click(screen.getByRole('button', { name: 'Добавить' }))
      await waitFor(() => expect(sensorsApi.addProject).toHaveBeenCalledWith('s1', 'p3'))
      expect(sensorsApi.decideProjectRequest).toHaveBeenCalledWith('r1', 'approve', 'p1')
      await waitFor(() => expect(notifySuccess).toHaveBeenCalledWith('Проект добавлен'))
      await waitFor(() => expect(screen.queryByText('Добавить проект', { selector: 'h2' })).not.toBeInTheDocument())
    })

    it('leaves the request pending when it cannot be approved from here', async () => {
      const user = userEvent.setup()
      vi.mocked(sensorsApi.addProject).mockResolvedValue({ id: 'r1' } as never)
      vi.mocked(sensorsApi.decideProjectRequest).mockRejectedValue({ response: { status: 403 } })
      renderModal()
      await screen.findByRole('table')
      await user.click(screen.getByRole('button', { name: 'Добавить проект' }))
      await pickMaterialSelectOption(user, /^Проект/, 'Third')
      await user.click(screen.getByRole('button', { name: 'Добавить' }))
      await waitFor(() =>
        expect(notifySuccess).toHaveBeenCalledWith(
          'Запрос отправлен, проект добавится после подтверждения владельцем датчика'
        )
      )
      expect(notifyError).not.toHaveBeenCalled()
    })

    it('cancels and closes the add dialog (via Отмена, ×, and overlay)', async () => {
      const user = userEvent.setup()
      renderModal()
      await screen.findByRole('table')
      const open = () => user.click(screen.getByRole('button', { name: 'Добавить проект' }))
      const heading = () => screen.queryByRole('heading', { name: 'Добавить проект', level: 2 })

      await open()
      expect(heading()).toBeInTheDocument()
      await user.click(screen.getByRole('button', { name: 'Отмена' }))
      expect(heading()).not.toBeInTheDocument()

      await open()
      await user.click(within(document.querySelector('.sensor-detail-add-project-overlay') as HTMLElement).getByRole('button', { name: '×' }))
      expect(heading()).not.toBeInTheDocument()

      await open()
      await user.click(document.querySelector('.sensor-detail-add-project-overlay') as HTMLElement)
      expect(heading()).not.toBeInTheDocument()
    })

    it('tells the user when every project is already attached', async () => {
      const user = userEvent.setup()
      vi.mocked(sensorsApi.getProjects).mockResolvedValue({ project_ids: ['p1', 'p2', 'p3'] })
      renderModal()
      await screen.findByRole('table')
      await user.click(screen.getByRole('button', { name: 'Добавить проект' }))
      expect(screen.getByText('Все доступные проекты уже добавлены к датчику')).toBeInTheDocument()
    })

    it('shows the add error and keeps the dialog open', async () => {
      const user = userEvent.setup()
      vi.mocked(sensorsApi.addProject).mockRejectedValue(new Error('forbidden'))
      renderModal()
      await screen.findByRole('table')
      await user.click(screen.getByRole('button', { name: 'Добавить проект' }))
      await pickMaterialSelectOption(user, /^Проект/, 'Third')
      await user.click(screen.getByRole('button', { name: 'Добавить' }))
      expect(await screen.findByText(/forbidden|Ошибка при добавлении проекта/)).toBeInTheDocument()
      expect(screen.getByRole('heading', { name: 'Добавить проект', level: 2 })).toBeInTheDocument()
    })
  })
})
