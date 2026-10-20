import { describe, it, expect, vi, beforeEach } from 'vitest'
import { MemoryRouter } from 'react-router-dom'
import { act, fireEvent, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import type { ReactNode } from 'react'
import CreateSensor from './CreateSensor'
import { sensorsApi, projectsApi } from '../api/client'
import { createMockProject, createMockSensor } from '../testUtils/factories'
import { pickMaterialSelectOption } from '../testUtils/materialSelect'
import { notifySuccess, notifySuccessSticky } from '../utils/notify'

vi.mock('../api/client', () => ({
    sensorsApi: { create: vi.fn(), addProject: vi.fn() },
    projectsApi: { list: vi.fn() },
}))

vi.mock('../utils/notify', () => ({
    notifyError: vi.fn(),
    notifySuccess: vi.fn(),
    notifySuccessSticky: vi.fn(),
}))

const mockNavigate = vi.fn()
vi.mock('react-router-dom', async () => {
    const actual = await vi.importActual('react-router-dom')
    return { ...actual, useNavigate: () => mockNavigate }
})

const wrapper = ({ children }: { children: ReactNode }) => {
    const qc = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } })
    return (
        <QueryClientProvider client={qc}>
            <MemoryRouter>{children}</MemoryRouter>
        </QueryClientProvider>
    )
}

const projects = [
    createMockProject({ id: 'project-1', name: 'P One' }),
    createMockProject({ id: 'project-2', name: 'P Two' }),
]

const fillForm = async (user: ReturnType<typeof userEvent.setup>, projectIds: string[]) => {
    await screen.findByText('P One')
    await user.selectOptions(screen.getByLabelText(/проекты/i), projectIds)
    await user.type(screen.getByLabelText(/название/i), '  Sensor X  ')
    await pickMaterialSelectOption(user, /тип датчика/i, 'Давление')
    await user.type(screen.getByLabelText(/входная единица измерения/i), 'mV')
    await user.type(screen.getByLabelText(/единица отображения/i), 'Pa')
}

const submit = () => {
    fireEvent.submit(document.querySelector('form.sensor-form') as HTMLFormElement)
}

describe('CreateSensor branches', () => {
    beforeEach(() => {
        vi.clearAllMocks()
        vi.mocked(projectsApi.list).mockResolvedValue({ projects, total: 2 } as never)
    })

    it('shows loading while projects load', () => {
        vi.mocked(projectsApi.list).mockImplementation(() => new Promise(() => {}))
        render(<CreateSensor />, { wrapper })
        expect(screen.getByText(/загрузка/i)).toBeInTheDocument()
    })

    it('shows hint when user has no projects', async () => {
        vi.mocked(projectsApi.list).mockResolvedValue({ projects: [], total: 0 } as never)
        render(<CreateSensor />, { wrapper })
        expect(await screen.findByRole('link', { name: 'Перейти к проектам' })).toHaveAttribute('href', '/projects')
    })

    it('shows per-field validation errors on empty submit', async () => {
        render(<CreateSensor />, { wrapper })
        await screen.findByText('P One')
        submit()
        expect(await screen.findAllByText('Выберите хотя бы один проект')).toHaveLength(2)
        expect(screen.getByText('Название датчика обязательно')).toHaveClass('field-error')
        expect(screen.getByText('Выберите тип датчика')).toHaveClass('field-error')
        expect(screen.getByText('Укажите входную единицу измерения')).toHaveClass('field-error')
        expect(screen.getByText('Укажите единицу отображения')).toHaveClass('field-error')
        expect(sensorsApi.create).not.toHaveBeenCalled()
    })

    it('resets primary project when selection is cleared', async () => {
        const user = userEvent.setup()
        render(<CreateSensor />, { wrapper })
        await screen.findByText('P One')
        const select = screen.getByLabelText(/проекты/i)
        await user.selectOptions(select, ['project-1'])
        await user.deselectOptions(select, ['project-1'])
        submit()
        expect(await screen.findAllByText('Выберите хотя бы один проект')).toHaveLength(2)
    })

    it('submits with calibration notes, shows pending and adding state, then error from extra projects', async () => {
        const user = userEvent.setup()
        let resolveCreate: (v: unknown) => void = () => {}
        vi.mocked(sensorsApi.create).mockImplementation(
            () => new Promise((r) => { resolveCreate = r as (v: unknown) => void })
        )
        let rejectAdd: (e: unknown) => void = () => {}
        vi.mocked(sensorsApi.addProject).mockImplementation(
            () => new Promise((_, rej) => { rejectAdd = rej })
        )
        render(<CreateSensor />, { wrapper })
        await fillForm(user, ['project-1', 'project-2'])
        await user.type(screen.getByLabelText(/заметки по калибровке/i), 'note')
        submit()
        expect(await screen.findByRole('button', { name: 'Регистрация...' })).toBeDisabled()
        expect(sensorsApi.create).toHaveBeenCalledWith({
            project_id: 'project-1',
            name: 'Sensor X',
            type: 'pressure',
            input_unit: 'mV',
            display_unit: 'Pa',
            calibration_notes: 'note',
        })

        await act(async () => {
            resolveCreate({ sensor: createMockSensor({ id: 'new-s', project_id: 'project-1' }), token: 'tkn' })
        })
        expect(await screen.findByText('Добавляем датчик в дополнительные проекты...')).toBeInTheDocument()
        expect(sensorsApi.addProject).toHaveBeenCalledWith('new-s', 'project-2')

        await act(async () => {
            rejectAdd({ response: { status: 500 } })
        })
        expect(await screen.findByText('Не удалось добавить датчик в дополнительные проекты')).toBeInTheDocument()
        expect(notifySuccess).not.toHaveBeenCalledWith('Датчик добавлен в дополнительные проекты')

        await user.click(screen.getByRole('button', { name: 'Перейти к датчику' }))
        expect(mockNavigate).toHaveBeenCalledWith('/sensors/new-s')
    })

    it('does not copy when token is empty, and cancel navigates', async () => {
        const user = userEvent.setup()
        render(<CreateSensor />, { wrapper })
        await screen.findByText('P One')
        await user.click(screen.getByRole('button', { name: 'Отмена' }))
        expect(mockNavigate).toHaveBeenCalledWith('/sensors')

        vi.mocked(sensorsApi.create).mockResolvedValueOnce({
            sensor: createMockSensor({ id: 'new-s', project_id: 'project-1' }),
            token: '',
        })
        await fillForm(user, ['project-1'])
        submit()
        expect(await screen.findByText('Датчик успешно зарегистрирован!')).toBeInTheDocument()
        await user.click(screen.getByRole('button', { name: 'Копировать' }))
        expect(notifySuccessSticky).not.toHaveBeenCalled()
        await waitFor(() => expect(notifySuccess).toHaveBeenCalledWith('Датчик зарегистрирован'))
    })
})
