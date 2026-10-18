import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import ConversionProfileCreateModal from './ConversionProfileCreateModal'
import { conversionProfilesApi } from '../api/client'

vi.mock('../api/client', () => ({
  conversionProfilesApi: { create: vi.fn() },
}))
vi.mock('../utils/notify', () => ({
  notifyError: vi.fn(), notifySuccess: vi.fn(), notifyInfo: vi.fn(), notifyWarning: vi.fn(),
}))

function renderModal(onClose = vi.fn()) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false }, mutations: { retry: false } } })
  render(
    <QueryClientProvider client={qc}>
      <ConversionProfileCreateModal sensorId="s1" isOpen onClose={onClose} />
    </QueryClientProvider>
  )
  return { onClose }
}

const form = () => screen.getByRole('button', { name: 'Создать профиль' }).closest('form') as HTMLElement
const removers = () => within(form()).queryAllByRole('button', { name: '×' })

async function setNumber(user: ReturnType<typeof userEvent.setup>, el: HTMLElement, value: string) {
  await user.clear(el)
  await user.type(el, value)
}

describe('ConversionProfileCreateModal', () => {
  beforeEach(() => vi.clearAllMocks())

  it('renders nothing when closed', () => {
    const qc = new QueryClient()
    render(
      <QueryClientProvider client={qc}>
        <ConversionProfileCreateModal sensorId="s1" isOpen={false} onClose={vi.fn()} />
      </QueryClientProvider>
    )
    expect(screen.queryByText('Создать профиль преобразования')).not.toBeInTheDocument()
  })

  it('previews the default linear conversion and updates with raw/a/b', async () => {
    const user = userEvent.setup()
    renderModal()
    expect(screen.getByText('10.0000')).toBeInTheDocument()
    await setNumber(user, screen.getByLabelText('a'), '2')
    await setNumber(user, screen.getByLabelText('b'), '1')
    expect(screen.getByText('21.0000')).toBeInTheDocument()
    const raw = screen.getByText('raw =').nextElementSibling as HTMLElement
    await setNumber(user, raw, '3')
    expect(screen.getByText('7.0000')).toBeInTheDocument()
  })

  it('requires a version', async () => {
    const user = userEvent.setup()
    renderModal()
    // bypass native `required` validation so our own check runs
    const form = screen.getByRole('button', { name: 'Создать профиль' }).closest('form') as HTMLFormElement
    form.noValidate = true
    await user.click(screen.getByRole('button', { name: 'Создать профиль' }))
    expect(await screen.findByRole('alert')).toHaveTextContent('Версия обязательна')
    expect(conversionProfilesApi.create).not.toHaveBeenCalled()
  })

  it('creates a linear profile and closes', async () => {
    const user = userEvent.setup()
    vi.mocked(conversionProfilesApi.create).mockResolvedValue({} as never)
    const { onClose } = renderModal()
    await user.type(screen.getByLabelText(/Версия/), '  v1  ')
    await setNumber(user, screen.getByLabelText('a'), '2.5')
    await user.click(screen.getByRole('button', { name: 'Создать профиль' }))
    await waitFor(() => expect(conversionProfilesApi.create).toHaveBeenCalled())
    expect(vi.mocked(conversionProfilesApi.create).mock.calls[0]).toEqual([
      's1',
      { version: 'v1', kind: 'linear', payload: { a: 2.5, b: 0 } },
    ])
    await waitFor(() => expect(onClose).toHaveBeenCalled())
  })

  it('edits polynomial coefficients, previews and submits', async () => {
    const user = userEvent.setup()
    vi.mocked(conversionProfilesApi.create).mockResolvedValue({} as never)
    renderModal()
    await user.type(screen.getByLabelText(/Версия/), 'p1')
    await user.selectOptions(screen.getByLabelText('Тип преобразования'), 'polynomial')
    expect(screen.getByText('c0')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '+ Добавить коэффициент' }))
    const inputs = () => screen.getAllByRole('spinbutton').slice(0, -1) // last one is the preview input
    expect(inputs()).toHaveLength(3)
    await setNumber(user, inputs()[0], '1')
    await setNumber(user, inputs()[2], '2')
    // 1 + 1*10 + 2*100 = 211
    expect(screen.getByText('211.0000')).toBeInTheDocument()
    // remove c1 -> 1 + 2*10 = 21
    await user.click(removers()[1])
    expect(inputs()).toHaveLength(2)
    expect(screen.getByText('21.0000')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'Создать профиль' }))
    await waitFor(() => expect(conversionProfilesApi.create).toHaveBeenCalled())
    expect(vi.mocked(conversionProfilesApi.create).mock.calls[0][1]).toEqual({
      version: 'p1', kind: 'polynomial', payload: { coefficients: [1, 2] },
    })
  })

  it('hides the remove button when a single coefficient remains', async () => {
    const user = userEvent.setup()
    renderModal()
    await user.selectOptions(screen.getByLabelText('Тип преобразования'), 'polynomial')
    await user.click(removers()[0])
    expect(removers()).toHaveLength(0)
  })

  it('edits the lookup table, interpolates and clamps', async () => {
    const user = userEvent.setup()
    vi.mocked(conversionProfilesApi.create).mockResolvedValue({} as never)
    renderModal()
    await user.type(screen.getByLabelText(/Версия/), 't1')
    await user.selectOptions(screen.getByLabelText('Тип преобразования'), 'lookup_table')
    const raw = screen.getByText('raw =').nextElementSibling as HTMLElement
    const points = () => screen.getAllByRole('spinbutton').slice(0, -1)
    expect(points()).toHaveLength(4)
    // default table 0->0, 100->100; raw 10 -> 10
    expect(screen.getByText('10.0000')).toBeInTheDocument()
    await setNumber(user, points()[3], '200')
    expect(screen.getByText('20.0000')).toBeInTheDocument()
    // clamp below and above
    await setNumber(user, raw, '-5')
    expect(screen.getByText('0.0000')).toBeInTheDocument()
    await setNumber(user, raw, '500')
    expect(screen.getByText('200.0000')).toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: '+ Добавить точку' }))
    expect(points()).toHaveLength(6)
    await setNumber(user, points()[4], '50')
    await setNumber(user, points()[5], '1000')
    // unsorted input is handled: raw=75 between (50,1000) and (100,200)
    await setNumber(user, raw, '75')
    expect(screen.getByText('600.0000')).toBeInTheDocument()

    // remove the added point (three rows -> removable)
    await user.click(removers()[2])
    expect(points()).toHaveLength(4)

    await user.click(screen.getByRole('button', { name: 'Создать профиль' }))
    await waitFor(() => expect(conversionProfilesApi.create).toHaveBeenCalled())
    expect(vi.mocked(conversionProfilesApi.create).mock.calls[0][1]).toEqual({
      version: 't1', kind: 'lookup_table', payload: { table: [{ raw: 0, physical: 0 }, { raw: 100, physical: 200 }] },
    })
  })

  it('does not offer removing rows from a two-point table', async () => {
    const user = userEvent.setup()
    renderModal()
    await user.selectOptions(screen.getByLabelText('Тип преобразования'), 'lookup_table')
    expect(removers()).toHaveLength(0)
  })

  it('shows a duplicate-x lookup preview without dividing by zero', async () => {
    const user = userEvent.setup()
    renderModal()
    await user.selectOptions(screen.getByLabelText('Тип преобразования'), 'lookup_table')
    await user.click(screen.getByRole('button', { name: '+ Добавить точку' }))
    const points = () => screen.getAllByRole('spinbutton').slice(0, -1)
    await setNumber(user, points()[4], '50')
    await setNumber(user, points()[5], '1')
    await setNumber(user, points()[2], '50')
    await setNumber(user, points()[3], '9')
    const raw = screen.getByText('raw =').nextElementSibling as HTMLElement
    await setNumber(user, raw, '50')
    expect(screen.queryByText('NaN')).not.toBeInTheDocument()
  })

  it('shows the server error inline and keeps the modal open', async () => {
    const user = userEvent.setup()
    vi.mocked(conversionProfilesApi.create).mockRejectedValue(new Error('duplicate version'))
    const { onClose } = renderModal()
    await user.type(screen.getByLabelText(/Версия/), 'v1')
    await user.click(screen.getByRole('button', { name: 'Создать профиль' }))
    expect(await screen.findByRole('alert')).toBeInTheDocument()
    expect(onClose).not.toHaveBeenCalled()
  })

  it('resets and closes on cancel', async () => {
    const user = userEvent.setup()
    const { onClose } = renderModal()
    await user.type(screen.getByLabelText(/Версия/), 'junk')
    await user.click(screen.getByRole('button', { name: 'Отмена' }))
    expect(onClose).toHaveBeenCalled()
  })

  it('disables the form while saving', async () => {
    const user = userEvent.setup()
    let release: (v: unknown) => void = () => {}
    vi.mocked(conversionProfilesApi.create).mockReturnValue(new Promise((r) => { release = r }) as never)
    renderModal()
    await user.type(screen.getByLabelText(/Версия/), 'v1')
    await user.click(screen.getByRole('button', { name: 'Создать профиль' }))
    const saving = await screen.findByRole('button', { name: 'Создание...' })
    expect(saving).toBeDisabled()
    expect(within(saving.closest('form') as HTMLElement).getByRole('button', { name: 'Отмена' })).toBeDisabled()
    release({})
  })
})
