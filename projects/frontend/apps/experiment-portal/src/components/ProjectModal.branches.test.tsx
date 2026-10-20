import { describe, it, expect, vi, beforeEach } from 'vitest'
import { act, fireEvent, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import type { ReactNode } from 'react'
import ProjectModal from './ProjectModal'
import { projectsApi } from '../api/client'
import { authApi } from '../api/auth'
import { createMockProject, createMockProjectMember, createMockUser } from '../testUtils/factories'

vi.mock('../api/client', () => ({
    projectsApi: { get: vi.fn(), create: vi.fn(), update: vi.fn(), listMembers: vi.fn() },
}))
vi.mock('../api/auth', () => ({
    authApi: { me: vi.fn() },
}))

const wrapper = ({ children }: { children: ReactNode }) => {
    const qc = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } })
    return <QueryClientProvider client={qc}>{children}</QueryClientProvider>
}

const project = createMockProject({ id: 'p1', name: 'Proj', description: 'Desc', owner_id: 'owner-1' })

const form = () => document.querySelector('form.modal-form') as HTMLFormElement

describe('ProjectModal branches', () => {
    const onClose = vi.fn()

    beforeEach(() => {
        vi.clearAllMocks()
        vi.mocked(authApi.me).mockResolvedValue(createMockUser({ id: 'owner-1' }))
        vi.mocked(projectsApi.get).mockResolvedValue(project)
        vi.mocked(projectsApi.listMembers).mockResolvedValue({ members: [] })
    })

    it('renders nothing when closed', () => {
        render(<ProjectModal isOpen={false} onClose={onClose} mode="view" projectId="p1" />, { wrapper })
        expect(screen.queryByText('Проект: просмотр')).not.toBeInTheDocument()
        expect(projectsApi.get).not.toHaveBeenCalled()
    })

    it('create: validates empty name, sends undefined description, shows inline error', async () => {
        const user = userEvent.setup()
        vi.mocked(projectsApi.create).mockRejectedValueOnce({ response: { status: 500 } })
        render(<ProjectModal isOpen onClose={onClose} mode="create" />, { wrapper })
        expect(screen.getByRole('button', { name: 'Создать проект' })).toBeDisabled()
        expect(screen.getByRole('button', { name: 'Отмена' })).toBeInTheDocument()
        expect(screen.queryByText('Информация')).not.toBeInTheDocument()

        fireEvent.submit(form())
        expect(await screen.findByRole('alert')).toHaveTextContent('Название проекта обязательно')

        await user.type(screen.getByLabelText(/название/i), '  New  ')
        await user.type(screen.getByLabelText(/описание/i), '   ')
        await user.click(screen.getByRole('button', { name: 'Создать проект' }))
        await waitFor(() => expect(projectsApi.create).toHaveBeenCalledWith({ name: 'New', description: undefined }))
        expect(await screen.findByText('Ошибка создания проекта')).toBeInTheDocument()
        expect(onClose).not.toHaveBeenCalled()
    })

    it('create: shows saving label while pending and blocks close', async () => {
        const user = userEvent.setup()
        let resolve: (v: unknown) => void = () => {}
        vi.mocked(projectsApi.create).mockImplementation(() => new Promise((r) => { resolve = r as (v: unknown) => void }))
        render(<ProjectModal isOpen onClose={onClose} mode="create" />, { wrapper })
        await user.type(screen.getByLabelText(/название/i), 'X')
        await user.type(screen.getByLabelText(/описание/i), 'D')
        await user.click(screen.getByRole('button', { name: 'Создать проект' }))
        expect(await screen.findByRole('button', { name: 'Сохранение...' })).toBeDisabled()
        expect(screen.getByRole('button', { name: 'Отмена' })).toBeDisabled()
        expect(projectsApi.create).toHaveBeenCalledWith({ name: 'X', description: 'D' })
        await act(async () => {
            resolve({})
        })
        await waitFor(() => expect(onClose).toHaveBeenCalled())
    })

    it('view: shows project load error', async () => {
        vi.mocked(projectsApi.get).mockRejectedValue({ response: { status: 500 } })
        render(<ProjectModal isOpen onClose={onClose} mode="view" projectId="p1" />, { wrapper })
        expect(await screen.findByText('Ошибка загрузки проекта')).toBeInTheDocument()
        expect(screen.queryByLabelText(/название/i)).not.toBeInTheDocument()
    })

    it('view: shows members load error', async () => {
        vi.mocked(authApi.me).mockResolvedValue(createMockUser({ id: 'u2' }))
        vi.mocked(projectsApi.listMembers).mockRejectedValue({ response: { status: 503 } })
        render(<ProjectModal isOpen onClose={onClose} mode="view" projectId="p1" />, { wrapper })
        expect(await screen.findByText('Ошибка загрузки проекта')).toBeInTheDocument()
    })

    it('view without projectId: read-only viewer with empty fields; submit is ignored', async () => {
        render(<ProjectModal isOpen onClose={onClose} mode="view" />, { wrapper })
        expect(await screen.findByText(/редактирование недоступно/)).toBeInTheDocument()
        expect(screen.getByText('Наблюдатель')).toBeInTheDocument()
        expect(screen.getByLabelText(/название/i)).toHaveValue('')
        expect(screen.getByLabelText(/название/i)).toBeDisabled()
        fireEvent.submit(form())
        expect(screen.queryByRole('alert')).not.toBeInTheDocument()
        expect(projectsApi.update).not.toHaveBeenCalled()
        await userEvent.setup().click(screen.getByRole('button', { name: 'Закрыть' }))
        expect(onClose).toHaveBeenCalled()
    })

    it('edit mode for member without builtin role forces view; null name/description fall back to empty', async () => {
        vi.mocked(authApi.me).mockResolvedValue(createMockUser({ id: 'u3' }))
        vi.mocked(projectsApi.get).mockResolvedValue({ ...project, name: null, description: null } as never)
        vi.mocked(projectsApi.listMembers).mockResolvedValue({
            members: [createMockProjectMember({ project_id: 'p1', user_id: 'u3', roles: ['custom'] })],
        })
        render(<ProjectModal isOpen onClose={onClose} mode="edit" projectId="p1" />, { wrapper })
        expect(await screen.findByText('Проект: просмотр')).toBeInTheDocument()
        await screen.findByText('Информация')
        expect(screen.getAllByText('Наблюдатель').length).toBeGreaterThan(0)
        expect(screen.getByLabelText(/название/i)).toHaveValue('')
        expect(screen.getByLabelText(/описание/i)).toHaveValue('')
        expect(screen.queryByRole('button', { name: 'Редактировать' })).not.toBeInTheDocument()
    })

    it('edit mode for user missing from members list (undefined members) is viewer', async () => {
        vi.mocked(authApi.me).mockResolvedValue(createMockUser({ id: 'u4' }))
        vi.mocked(projectsApi.listMembers).mockResolvedValue({} as never)
        render(<ProjectModal isOpen onClose={onClose} mode="edit" projectId="p1" />, { wrapper })
        expect(await screen.findByText(/редактирование недоступно/)).toBeInTheDocument()
    })

    it('owner in edit mode: cancel restores values, re-edit, update error shown', async () => {
        const user = userEvent.setup()
        vi.mocked(projectsApi.update).mockRejectedValueOnce({ response: { status: 500 } })
        render(<ProjectModal isOpen onClose={onClose} mode="edit" projectId="p1" />, { wrapper })
        expect(await screen.findByText('Проект: просмотр и редактирование')).toBeInTheDocument()
        expect(await screen.findByText('Владелец')).toBeInTheDocument()
        const name = screen.getByLabelText(/название/i)
        await waitFor(() => expect(name).toHaveValue('Proj'))
        await user.clear(name)
        await user.type(name, 'Changed')
        await user.click(screen.getByRole('button', { name: 'Отмена' }))
        expect(screen.getByLabelText(/название/i)).toHaveValue('Proj')
        expect(screen.getByText('Проект: просмотр')).toBeInTheDocument()

        await user.click(screen.getByRole('button', { name: 'Редактировать' }))
        await user.clear(screen.getByLabelText(/описание/i))
        await user.click(screen.getByRole('button', { name: 'Сохранить' }))
        await waitFor(() => expect(projectsApi.update).toHaveBeenCalledWith('p1', { name: 'Proj', description: undefined }))
        expect(await screen.findByText('Ошибка обновления проекта')).toBeInTheDocument()
    })
})
