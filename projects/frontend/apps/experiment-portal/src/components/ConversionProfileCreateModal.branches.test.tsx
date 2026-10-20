import { describe, it, expect, vi, beforeEach } from 'vitest'
import { fireEvent, render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ConversionProfileCreateModal from './ConversionProfileCreateModal'
import { conversionProfilesApi } from '../api/client'

vi.mock('../api/client', () => ({
    conversionProfilesApi: { create: vi.fn() },
}))
vi.mock('../utils/notify', () => ({
    notifyError: vi.fn(),
    notifySuccess: vi.fn(),
}))

function renderModal() {
    const qc = new QueryClient({ defaultOptions: { mutations: { retry: false } } })
    render(
        <QueryClientProvider client={qc}>
            <ConversionProfileCreateModal sensorId="s1" isOpen onClose={vi.fn()} />
        </QueryClientProvider>
    )
}

/** Number inputs sanitize non-numeric text in jsdom; switch to text to feed garbage through onChange. */
function typeGarbage(el: HTMLElement, value = 'abc') {
    el.setAttribute('type', 'text')
    fireEvent.change(el, { target: { value } })
}

const previewInput = () => screen.getByText('raw =').nextElementSibling as HTMLElement
const previewValue = () => screen.getByText(/→ physical =/).nextElementSibling as HTMLElement

describe('ConversionProfileCreateModal invalid input branches', () => {
    beforeEach(() => vi.clearAllMocks())

    it('linear: non-numeric coefficient shows dash preview and rejects submit', async () => {
        const user = userEvent.setup()
        renderModal()
        await user.type(screen.getByLabelText(/Версия/), 'v1')
        typeGarbage(screen.getByLabelText('a'))
        expect(previewValue()).toHaveTextContent('—')
        await user.click(screen.getByRole('button', { name: 'Создать профиль' }))
        expect(await screen.findByRole('alert')).toHaveTextContent('Некорректные параметры преобразования')
        expect(conversionProfilesApi.create).not.toHaveBeenCalled()
    })

    it('non-numeric preview raw value shows dash', () => {
        renderModal()
        expect(previewValue()).toHaveTextContent('10.0000')
        typeGarbage(previewInput())
        expect(previewValue()).toHaveTextContent('—')
    })

    it('polynomial: non-numeric coefficient rejects submit', async () => {
        const user = userEvent.setup()
        renderModal()
        await user.type(screen.getByLabelText(/Версия/), 'v1')
        await user.selectOptions(screen.getByLabelText('Тип преобразования'), 'polynomial')
        typeGarbage(screen.getAllByRole('spinbutton')[0])
        expect(previewValue()).toHaveTextContent('—')
        await user.click(screen.getByRole('button', { name: 'Создать профиль' }))
        expect(await screen.findByRole('alert')).toHaveTextContent('Некорректные параметры преобразования')
    })

    it('lookup table: non-numeric point rejects submit', async () => {
        const user = userEvent.setup()
        renderModal()
        await user.type(screen.getByLabelText(/Версия/), 'v1')
        await user.selectOptions(screen.getByLabelText('Тип преобразования'), 'lookup_table')
        typeGarbage(screen.getAllByRole('spinbutton')[1])
        expect(previewValue()).toHaveTextContent('—')
        await user.click(screen.getByRole('button', { name: 'Создать профиль' }))
        expect(await screen.findByRole('alert')).toHaveTextContent('Некорректные параметры преобразования')
    })
})
