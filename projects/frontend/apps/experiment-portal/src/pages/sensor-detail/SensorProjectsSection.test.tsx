import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import SensorProjectsSection from './SensorProjectsSection'
import { pickMaterialSelectOption } from '../../testUtils/materialSelect'

type Props = React.ComponentProps<typeof SensorProjectsSection>

const projects = [
  { id: 'p1', name: 'Main' },
  { id: 'p2', name: 'Second' },
  { id: 'p3', name: 'Third' },
]

function mutation(over: Partial<Props['addProjectMutation']> = {}) {
  return { mutate: vi.fn(), isPending: false, isError: false, error: null, ...over }
}

function setup(over: Partial<Props> = {}) {
  const props: Props = {
    sensor: { id: 's1', project_id: 'p1' } as never,
    sensorProjectsData: { project_ids: ['p1', 'p2'] },
    isLoadingProjects: false,
    allProjectsData: { projects } as never,
    showAddProjectModal: false,
    selectedProjectId: '',
    addProjectMutation: mutation(),
    removeProjectMutation: mutation(),
    setShowAddProjectModal: vi.fn(),
    setSelectedProjectId: vi.fn(),
    ...over,
  }
  render(<SensorProjectsSection {...props} />)
  return props
}

describe('SensorProjectsSection', () => {
  let confirmSpy: ReturnType<typeof vi.spyOn>
  beforeEach(() => { confirmSpy = vi.spyOn(window, 'confirm').mockReturnValue(true) })
  afterEach(() => confirmSpy.mockRestore())

  it('lists projects, flags the primary one and names unknown ones', () => {
    setup({ sensorProjectsData: { project_ids: ['p1', 'p2', 'ghost'] } })
    expect(screen.getByText('Основной')).toBeInTheDocument()
    expect(screen.getByText('Main')).toBeInTheDocument()
    expect(screen.getByText('Неизвестный проект')).toBeInTheDocument()
    expect(screen.getByText('Нельзя удалить основной проект')).toBeInTheDocument()
    expect(screen.getAllByRole('button', { name: 'Удалить' })).toHaveLength(2)
  })

  it('works before project names have loaded', () => {
    setup({ allProjectsData: undefined })
    expect(screen.getAllByText('Неизвестный проект')).toHaveLength(2)
  })

  it('shows a loader while loading and nothing for missing data', () => {
    setup({ isLoadingProjects: true })
    expect(screen.queryByRole('table')).not.toBeInTheDocument()
  })

  it('renders no table when data is missing', () => {
    setup({ sensorProjectsData: undefined })
    expect(screen.queryByRole('table')).not.toBeInTheDocument()
  })

  it('shows the empty state', () => {
    setup({ sensorProjectsData: { project_ids: [] } })
    expect(screen.getByText('Датчик не привязан ни к одному проекту')).toBeInTheDocument()
  })

  it('opens the add dialog', async () => {
    const user = userEvent.setup()
    const props = setup()
    await user.click(screen.getByRole('button', { name: 'Добавить проект' }))
    expect(props.setShowAddProjectModal).toHaveBeenCalledWith(true)
  })

  it('removes a project after confirmation, and not when declined', async () => {
    const user = userEvent.setup()
    const props = setup()
    await user.click(screen.getByRole('button', { name: 'Удалить' }))
    expect(confirmSpy).toHaveBeenCalledWith('Удалить датчик из проекта Second?')
    expect(props.removeProjectMutation.mutate).toHaveBeenCalledWith('p2')
    confirmSpy.mockReturnValue(false)
    await user.click(screen.getByRole('button', { name: 'Удалить' }))
    expect(props.removeProjectMutation.mutate).toHaveBeenCalledTimes(1)
  })

  it('falls back to the id in the confirmation for unknown projects', async () => {
    const user = userEvent.setup()
    setup({ sensorProjectsData: { project_ids: ['p1', 'ghost'] } })
    await user.click(screen.getByRole('button', { name: 'Удалить' }))
    expect(confirmSpy).toHaveBeenCalledWith('Удалить датчик из проекта ghost?')
  })

  it('disables actions while a mutation is pending', () => {
    setup({ removeProjectMutation: mutation({ isPending: true }) })
    expect(screen.getByRole('button', { name: 'Удалить' })).toBeDisabled()
    expect(screen.getByRole('button', { name: 'Добавить проект' })).toBeDisabled()
  })

  describe('add dialog', () => {
    it('selects a project, submits, and only offers unattached projects', async () => {
      const user = userEvent.setup()
      const props = setup({ showAddProjectModal: true, selectedProjectId: 'p3' })
      const dialog = screen.getByRole('heading', { name: 'Добавить проект', level: 2 }).closest('.modal-content') as HTMLElement
      expect(within(dialog).getByRole('button', { name: 'Добавить' })).toBeEnabled()
      await user.click(within(dialog).getByRole('button', { name: 'Добавить' }))
      expect(props.addProjectMutation.mutate).toHaveBeenCalledWith('p3')
    })

    it('does not submit without a selection', async () => {
      setup({ showAddProjectModal: true })
      const dialog = screen.getByRole('heading', { name: 'Добавить проект', level: 2 }).closest('.modal-content') as HTMLElement
      expect(within(dialog).getByRole('button', { name: 'Добавить' })).toBeDisabled()
    })

    it('reports the chosen project through the select', async () => {
      const user = userEvent.setup()
      const props = setup({ showAddProjectModal: true })
      await pickMaterialSelectOption(user, /^Проект/, 'Third')
      expect(props.setSelectedProjectId).toHaveBeenCalledWith('p3')
    })

    it('closes via Cancel, the × button and the overlay, clearing the selection where applicable', async () => {
      const user = userEvent.setup()
      const props = setup({ showAddProjectModal: true })
      await user.click(screen.getByRole('button', { name: 'Отмена' }))
      expect(props.setShowAddProjectModal).toHaveBeenLastCalledWith(false)
      expect(props.setSelectedProjectId).toHaveBeenLastCalledWith('')
      await user.click(screen.getByRole('button', { name: '×' }))
      expect(props.setSelectedProjectId).toHaveBeenCalledTimes(2)
      await user.click(document.querySelector('.modal-overlay') as HTMLElement)
      expect(props.setShowAddProjectModal).toHaveBeenCalledTimes(3)
      // clicking inside the dialog does not close it
      await user.click(screen.getByRole('heading', { name: 'Добавить проект', level: 2 }))
      expect(props.setShowAddProjectModal).toHaveBeenCalledTimes(3)
    })

    it('says so when every project is already attached', () => {
      setup({ showAddProjectModal: true, sensorProjectsData: { project_ids: ['p1', 'p2', 'p3'] } })
      expect(screen.getByText('Все доступные проекты уже добавлены к датчику')).toBeInTheDocument()
    })

    it('shows the mutation error message', () => {
      setup({ showAddProjectModal: true, addProjectMutation: mutation({ isError: true, error: new Error('no rights') }) })
      expect(screen.getByText('no rights')).toBeInTheDocument()
    })

    it('shows a generic error when the error has no message', () => {
      setup({ showAddProjectModal: true, addProjectMutation: mutation({ isError: true, error: 'weird' }) })
      expect(screen.getByText('Ошибка при добавлении проекта')).toBeInTheDocument()
    })

    it('shows progress and disables controls while adding', () => {
      setup({ showAddProjectModal: true, selectedProjectId: 'p3', addProjectMutation: mutation({ isPending: true }) })
      expect(screen.getByRole('button', { name: 'Добавление...' })).toBeDisabled()
      expect(screen.getByRole('button', { name: 'Отмена' })).toBeDisabled()
    })
  })
})
