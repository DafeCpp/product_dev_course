import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { act, render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { MemoryRouter } from 'react-router-dom'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import SensorDetailModal from './SensorDetailModal'
import { sensorsApi, projectsApi } from '../api/client'
import { notifySuccess } from '../utils/notify'
import { pickMaterialSelectOption } from '../testUtils/materialSelect'

vi.mock('../api/client', () => ({
    sensorsApi: {
        get: vi.fn(),
        getProjects: vi.fn(),
        delete: vi.fn(),
        rotateToken: vi.fn(),
        addProject: vi.fn(),
        removeProject: vi.fn(),
    },
    projectsApi: { list: vi.fn() },
}))
vi.mock('../utils/notify', () => ({
    notifyError: vi.fn(),
    notifySuccess: vi.fn(),
    notifySuccessSticky: vi.fn(),
}))
vi.mock('./TestTelemetryModal', () => ({ default: () => null }))
vi.mock('./TelemetryStreamModal', () => ({ default: () => null }))

function sensor(over: Record<string, unknown> = {}) {
    return {
        id: 's1',
        project_id: 'p1',
        name: 'Thermo',
        type: 'temperature',
        input_unit: 'mV',
        display_unit: 'C',
        status: 'active',
        token_preview: null,
        last_heartbeat: null,
        active_profile_id: null,
        calibration_notes: null,
        created_at: '2026-01-02T10:00:00Z',
        updated_at: '2026-01-03T11:30:00Z',
        ...over,
    }
}

function renderModal() {
    const qc = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } })
    return render(
        <QueryClientProvider client={qc}>
            <MemoryRouter>
                <SensorDetailModal isOpen onClose={vi.fn()} sensorId="s1" />
            </MemoryRouter>
        </QueryClientProvider>
    )
}

describe('SensorDetailModal branches', () => {
    const realClipboard = Object.getOwnPropertyDescriptor(navigator, 'clipboard')

    beforeEach(() => {
        vi.clearAllMocks()
        vi.spyOn(window, 'confirm').mockReturnValue(true)
        vi.mocked(sensorsApi.get).mockResolvedValue(sensor() as never)
        vi.mocked(sensorsApi.getProjects).mockResolvedValue({ project_ids: ['p1', 'p9'] })
        vi.mocked(projectsApi.list).mockResolvedValue({ projects: [{ id: 'p1', name: 'Main' }, { id: 'p3', name: 'Third' }] } as never)
    })

    afterEach(() => {
        vi.restoreAllMocks()
        if (realClipboard) Object.defineProperty(navigator, 'clipboard', realClipboard)
    })

    it('shows fallback error text for non-object errors', async () => {
        vi.mocked(sensorsApi.get).mockRejectedValue('boom')
        renderModal()
        expect(await screen.findByText('Ошибка загрузки датчика')).toBeInTheDocument()
    })

    it('shows projects loading indicator while sensor projects load', async () => {
        vi.mocked(sensorsApi.getProjects).mockImplementation(() => new Promise(() => {}))
        renderModal()
        await screen.findByRole('button', { name: 'Ротация токена' })
        expect(screen.getAllByText(/загрузка/i).length).toBeGreaterThan(0)
        expect(screen.queryByRole('table')).not.toBeInTheDocument()
    })

    it('shows rotating label while rotation is pending', async () => {
        const user = userEvent.setup()
        vi.mocked(sensorsApi.rotateToken).mockImplementation(() => new Promise(() => {}))
        renderModal()
        await user.click(await screen.findByRole('button', { name: 'Ротация токена' }))
        expect(await screen.findByRole('button', { name: 'Ротация...' })).toBeDisabled()
    })

    it('uses project id in remove confirmation for unknown projects', async () => {
        const user = userEvent.setup()
        vi.mocked(sensorsApi.removeProject).mockResolvedValue(undefined)
        renderModal()
        const table = await screen.findByRole('table')
        await user.click(within(table).getByRole('button', { name: 'Удалить' }))
        expect(window.confirm).toHaveBeenCalledWith('Удалить датчик из проекта p9?')
        await waitFor(() => expect(sensorsApi.removeProject).toHaveBeenCalledWith('s1', 'p9'))
    })

    it('shows pending add label and generic add error for non-object rejection', async () => {
        const user = userEvent.setup()
        let reject: (e: unknown) => void = () => {}
        vi.mocked(sensorsApi.addProject).mockImplementation(() => new Promise((_, rej) => { reject = rej }))
        renderModal()
        await screen.findByRole('table')
        await user.click(screen.getByRole('button', { name: 'Добавить проект' }))
        await pickMaterialSelectOption(user, /^Проект/, 'Third')
        await user.click(screen.getByRole('button', { name: 'Добавить' }))
        expect(await screen.findByRole('button', { name: 'Добавление...' })).toBeDisabled()
        await act(async () => {
            reject('nope')
        })
        expect(await screen.findByText('Ошибка при добавлении проекта')).toBeInTheDocument()
    })

    it('copies the prop sensor id when the sensor has an empty id', async () => {
        const user = userEvent.setup()
        vi.mocked(sensorsApi.get).mockResolvedValue(sensor({ id: '' }) as never)
        const writeText = vi.fn().mockResolvedValue(undefined)
        renderModal()
        const btn = await screen.findByRole('button', { name: 'Копировать ID датчика' })
        Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true })
        await user.click(btn)
        await waitFor(() => expect(writeText).toHaveBeenCalledWith('s1'))
        expect(notifySuccess).toHaveBeenCalledWith('ID датчика в буфере обмена', 'Скопировано')
    })
})
