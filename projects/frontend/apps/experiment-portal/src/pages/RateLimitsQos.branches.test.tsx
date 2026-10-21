import { describe, it, expect, beforeEach, vi } from 'vitest'
import { render, screen, waitFor, act } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { MemoryRouter } from 'react-router-dom'
import RateLimitsQos from './RateLimitsQos'
import type { ConfigResponse } from '../types/configs'

const mockListConfigs = vi.fn()
const mockCreateConfig = vi.fn()
const mockPatchConfig = vi.fn()
const mockDryRunCreate = vi.fn()
const mockDryRunPatch = vi.fn()

vi.mock('../api/configs', () => ({
  configsApi: {
    listConfigs: () => mockListConfigs(),
    createConfig: (data: unknown) => mockCreateConfig(data),
    patchConfig: (id: string, data: unknown) => mockPatchConfig(id, data),
    dryRunCreate: (data: unknown) => mockDryRunCreate(data),
    dryRunPatch: (id: string, data: unknown) => mockDryRunPatch(id, data),
  },
}))

let permsLoading = false
const mockHasPermission = vi.fn()
vi.mock('../hooks/usePermissions', () => ({
  usePermissions: () => ({
    hasSystemPermission: (perm: string) => mockHasPermission(perm),
    isLoading: permsLoading,
    user: null,
  }),
}))

const mockNotifyError = vi.fn()
vi.mock('../utils/notify', () => ({
  notifySuccess: vi.fn(),
  notifyError: (...args: unknown[]) => mockNotifyError(...args),
}))

type FormProps = {
  config: ConfigResponse | null
  onSave: (v: Record<string, unknown>) => Promise<void> | void
  isSaving?: boolean
}
function makeForm(name: string) {
  return function StubForm({ config, onSave, isSaving }: FormProps) {
    return (
      <div data-testid={`form-${name}`}>
        <span>{config ? `cfg:${config.id}` : 'no-config'}</span>
        <span>{isSaving ? 'saving' : 'idle'}</span>
        <button onClick={() => void onSave({ rps: 5 })}>
          save-{name}
        </button>
      </div>
    )
  }
}
vi.mock('./rateLimits/AuthQosForm', () => ({ AuthQosForm: makeForm('auth') }))
vi.mock('./rateLimits/ExperimentQosForm', () => ({ ExperimentQosForm: makeForm('experiment') }))
vi.mock('./rateLimits/TelemetryRateLimitsForm', () => ({
  TelemetryRateLimitsForm: makeForm('telemetry'),
}))

const EXP_CFG = {
  id: 'exp1',
  service_name: 'experiment-service',
  key: 'experiment_qos',
  version: 4,
  value: {},
} as unknown as ConfigResponse

function renderPage() {
  const queryClient = new QueryClient({
    defaultOptions: { queries: { retry: false }, mutations: { retry: false } },
  })
  return render(
    <QueryClientProvider client={queryClient}>
      <MemoryRouter>
        <RateLimitsQos />
      </MemoryRouter>
    </QueryClientProvider>,
  )
}

describe('RateLimitsQos — branches', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    permsLoading = false
    mockHasPermission.mockReturnValue(true)
    mockListConfigs.mockResolvedValue({ items: [EXP_CFG], next_cursor: null })
  })

  it('shows permission loading', () => {
    permsLoading = true
    renderPage()
    expect(screen.getByText('Проверка прав доступа...')).toBeInTheDocument()
  })

  it('switches between service forms', async () => {
    const user = userEvent.setup()
    renderPage()
    expect(await screen.findByTestId('form-auth')).toHaveTextContent('no-config')
    await user.click(screen.getByText('Experiment Service'))
    expect(screen.getByTestId('form-experiment')).toHaveTextContent('cfg:exp1')
    expect(screen.getByText('Experiment Service').className).toContain('btn--active')
    await user.click(screen.getByText('Telemetry Ingest'))
    expect(screen.getByTestId('form-telemetry')).toBeInTheDocument()
  })

  it('create flow: dry-run create, then confirm create', async () => {
    mockDryRunCreate.mockResolvedValue({ dry_run: true, preview: { id: 'preview-1', key: 'auth_qos', value: { rps: 5 } } })
    mockCreateConfig.mockResolvedValue({ config: EXP_CFG })
    const user = userEvent.setup()
    renderPage()
    await user.click(await screen.findByText('save-auth'))
    await waitFor(() =>
      expect(mockDryRunCreate).toHaveBeenCalledWith(
        expect.objectContaining({ service_name: 'auth-service', key: 'auth_qos', value: { rps: 5 } }),
      ),
    )
    expect(await screen.findByText('Проверка конфигурации')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'Подтвердить сохранение' }))
    await waitFor(() =>
      expect(mockCreateConfig).toHaveBeenCalledWith(
        expect.objectContaining({ service_name: 'auth-service', key: 'auth_qos', value: { rps: 5 } }),
      ),
    )
  })

  it.each([false, true])('creates telemetry config under the poller name (legacy config: %s)', async (hasLegacy) => {
    const legacy = { ...EXP_CFG, id: 'legacy', service_name: 'telemetry-ingest-service', key: 'rate_limits' }
    mockListConfigs.mockResolvedValue({ items: hasLegacy ? [legacy] : [], next_cursor: null })
    mockDryRunCreate.mockResolvedValue({ dry_run: true, preview: {} })
    mockCreateConfig.mockResolvedValue({ config: { ...legacy, id: 'canonical', service_name: 'telemetry-ingest' } })
    const user = userEvent.setup()
    renderPage()
    await user.click(await screen.findByText('Telemetry Ingest'))
    await user.click(await screen.findByText('save-telemetry'))
    const payload = { service_name: 'telemetry-ingest', key: 'rate_limits', config_type: 'qos', value: { rps: 5 }, is_critical: true }
    await waitFor(() => expect(mockDryRunCreate).toHaveBeenCalledWith(payload))
    await user.click(await screen.findByRole('button', { name: 'Подтвердить сохранение' }))
    await waitFor(() => expect(mockCreateConfig).toHaveBeenCalledWith(payload))
    expect(mockDryRunPatch).not.toHaveBeenCalled()
    expect(mockPatchConfig).not.toHaveBeenCalled()
  })

  it('updates the canonical telemetry config even when a legacy config is listed first', async () => {
    const legacy = { ...EXP_CFG, id: 'legacy', service_name: 'telemetry-ingest-service', key: 'rate_limits' }
    const canonical = { ...legacy, id: 'canonical', service_name: 'telemetry-ingest', version: 7 }
    mockListConfigs.mockResolvedValue({ items: [legacy, canonical], next_cursor: null })
    mockDryRunPatch.mockResolvedValue({ dry_run: true, preview: {} })
    mockPatchConfig.mockResolvedValue({ config: canonical })
    const user = userEvent.setup()
    renderPage()
    await user.click(await screen.findByText('Telemetry Ingest'))
    expect(await screen.findByText('cfg:canonical')).toBeInTheDocument()
    await user.click(screen.getByText('save-telemetry'))
    const payload = { version: 7, value: { rps: 5 }, change_reason: 'Updated via rate-limits page' }
    await waitFor(() => expect(mockDryRunPatch).toHaveBeenCalledWith('canonical', payload))
    await user.click(await screen.findByRole('button', { name: 'Подтвердить сохранение' }))
    await waitFor(() => expect(mockPatchConfig).toHaveBeenCalledWith('canonical', payload))
    expect(mockDryRunCreate).not.toHaveBeenCalled()
    expect(mockCreateConfig).not.toHaveBeenCalled()
  })

  it('patch flow: dry-run patch with pending state, then confirm patch', async () => {
    let resolveDry: (v: unknown) => void = () => {}
    mockDryRunPatch.mockReturnValue(new Promise((r) => (resolveDry = r)))
    let resolveSave: (v: unknown) => void = () => {}
    mockPatchConfig.mockReturnValue(new Promise((r) => (resolveSave = r)))
    const user = userEvent.setup()
    renderPage()
    await user.click(await screen.findByText('Experiment Service'))
    await user.click(screen.getByText('save-experiment'))
    expect(await screen.findByText('saving')).toBeInTheDocument()
    expect(mockDryRunPatch).toHaveBeenCalledWith('exp1', {
      version: 4,
      value: { rps: 5 },
      change_reason: 'Updated via rate-limits page',
    })
    await act(async () => {
      resolveDry({ dry_run: true, preview: { rps: 7 } })
    })
    expect(await screen.findByText(/"rps": 7/)).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: 'Подтвердить сохранение' }))
    expect(await screen.findByRole('button', { name: 'Сохраняю...' })).toBeDisabled()
    // Confirm saves the entered value, not the dry-run preview object.
    expect(mockPatchConfig).toHaveBeenCalledWith('exp1', expect.objectContaining({ value: { rps: 5 } }))
    await act(async () => {
      resolveSave({ config: EXP_CFG })
    })
    // The confirmed value cannot be saved twice without a new dry-run.
    await waitFor(() =>
      expect(screen.queryByRole('button', { name: 'Подтвердить сохранение' })).not.toBeInTheDocument(),
    )
  })

  it('dry-run 4xx error shows rejected message; switching service clears it', async () => {
    mockDryRunCreate.mockRejectedValue({ response: { status: 422 } })
    const user = userEvent.setup()
    renderPage()
    await user.click(await screen.findByText('save-auth'))
    expect(await screen.findByRole('alert')).toHaveTextContent('конфигурация отклонена сервером')
    await user.click(screen.getByText('Telemetry Ingest'))
    expect(screen.queryByRole('alert')).not.toBeInTheDocument()
  })

  it('dry-run 5xx error shows unavailable message', async () => {
    mockDryRunCreate.mockRejectedValue({ response: { status: 503 } })
    const user = userEvent.setup()
    renderPage()
    await user.click(await screen.findByText('Telemetry Ingest'))
    await user.click(screen.getByText('save-telemetry'))
    expect(await screen.findByRole('alert')).toHaveTextContent('сервис недоступен, попробуйте позже')
  })

  it('failed confirmed save shows an error toast without an unhandled rejection', async () => {
    mockDryRunPatch.mockResolvedValue({ dry_run: true, preview: { rps: 5 } })
    mockPatchConfig.mockRejectedValue(new Error('boom'))
    const user = userEvent.setup()
    renderPage()
    await user.click(await screen.findByText('Experiment Service'))
    await user.click(screen.getByText('save-experiment'))
    await user.click(await screen.findByRole('button', { name: 'Подтвердить сохранение' }))
    await waitFor(() => expect(mockNotifyError).toHaveBeenCalled())
    expect(screen.getByRole('button', { name: 'Подтвердить сохранение' })).toBeEnabled()
  })
})
