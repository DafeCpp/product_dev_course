import { describe, it, expect, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { AuthQosForm } from './AuthQosForm'
import { ExperimentQosForm } from './ExperimentQosForm'
import { TelemetryRateLimitsForm } from './TelemetryRateLimitsForm'
import type { ConfigResponse } from '../../types/configs'

const cfg = (value: unknown): ConfigResponse => ({ value } as ConfigResponse)

async function setNumber(el: HTMLElement, value: string) {
  await userEvent.clear(el)
  await userEvent.type(el, value)
}

describe('AuthQosForm', () => {
  it('uses defaults, submits parsed numbers', async () => {
    const onSave = vi.fn()
    render(<AuthQosForm config={null} onSave={onSave} />)
    const [access, refresh] = screen.getAllByRole('spinbutton')
    expect(access).toHaveValue(900)
    expect(refresh).toHaveValue(3600)
    await setNumber(access, '120')
    await userEvent.click(screen.getByText('Сохранить'))
    expect(onSave).toHaveBeenCalledWith({ access_token_ttl_sec: 120, refresh_token_ttl_sec: 3600 })
  })

  it('shows validation and API errors and clears a field error on edit', async () => {
    const onSave = vi.fn()
    render(<AuthQosForm config={cfg({ access_token_ttl_sec: 10, refresh_token_ttl_sec: 20 })} onSave={onSave}
      errors={{ refresh_token_ttl_sec: 'от сервера' }} />)
    const [access, refresh] = screen.getAllByRole('spinbutton')
    expect(access).toHaveValue(10)
    expect(screen.getByText('от сервера')).toBeInTheDocument()
    await userEvent.clear(access)
    await userEvent.click(screen.getByText('Сохранить'))
    expect(onSave).not.toHaveBeenCalled()
    expect(screen.getByText('Должно быть положительным целым числом')).toBeInTheDocument()
    expect(access).toHaveAttribute('aria-invalid', 'true')
    await userEvent.type(access, '5')
    expect(screen.queryByText('Должно быть положительным целым числом')).toBeNull()
    expect(refresh).toHaveAttribute('aria-invalid', 'false')
  })

  it('reflects the saving state', () => {
    render(<AuthQosForm config={null} onSave={vi.fn()} isSaving />)
    expect(screen.getByText('Сохраняю...')).toBeDisabled()
  })
})

describe('ExperimentQosForm', () => {
  it('submits defaults and edited values', async () => {
    const onSave = vi.fn()
    render(<ExperimentQosForm config={null} onSave={onSave} />)
    const [limit, timeout] = screen.getAllByRole('spinbutton')
    expect(limit).toHaveValue(1000)
    expect(timeout).toHaveValue(30)
    await setNumber(limit, '50')
    await setNumber(timeout, '2.5')
    await userEvent.click(screen.getByText('Сохранить'))
    expect(onSave).toHaveBeenCalledWith({ rate_limit_max_requests: 50, downstream_timeout_seconds: 2.5 })
  })

  it('rejects invalid values and shows API errors', async () => {
    const onSave = vi.fn()
    render(<ExperimentQosForm config={cfg({ rate_limit_max_requests: 5, downstream_timeout_seconds: 1 })} onSave={onSave}
      errors={{ rate_limit_max_requests: 'conflict', downstream_timeout_seconds: 'slow' }} />)
    expect(screen.getByText('conflict')).toBeInTheDocument()
    expect(screen.getByText('slow')).toBeInTheDocument()
    const [limit, timeout] = screen.getAllByRole('spinbutton')
    await userEvent.clear(limit)
    await setNumber(timeout, '0')
    await userEvent.click(screen.getByText('Сохранить'))
    expect(onSave).not.toHaveBeenCalled()
    expect(screen.getByText('Должно быть положительным числом')).toBeInTheDocument()
    await userEvent.type(limit, '1')
    expect(limit).toHaveAttribute('aria-invalid', 'false')
  })

  it('reflects the saving state', () => {
    render(<ExperimentQosForm config={null} onSave={vi.fn()} isSaving />)
    expect(screen.getByText('Сохраняю...')).toBeDisabled()
  })
})

describe('TelemetryRateLimitsForm', () => {
  it('submits the nested structure with defaults', async () => {
    const onSave = vi.fn()
    render(<TelemetryRateLimitsForm config={null} onSave={onSave} />)
    await userEvent.click(screen.getByText('Сохранить'))
    expect(onSave).toHaveBeenCalledWith({
      rest: { max_requests: 600, max_readings: 60000, window_seconds: 60 },
      ws: { max_messages: 600, max_readings: 60000, window_seconds: 1 },
      spool_flush_timeout_seconds: 5,
      ws_max_message_bytes: 1048576,
    })
  })

  it('loads values from the config and lets every field be edited', async () => {
    const onSave = vi.fn()
    render(<TelemetryRateLimitsForm onSave={onSave} config={cfg({
      rest: { max_requests: 1, max_readings: 2, window_seconds: 3 },
      ws: { max_messages: 4, max_readings: 5, window_seconds: 6 },
      spool_flush_timeout_seconds: 7, ws_max_message_bytes: 8,
    })} />)
    const inputs = screen.getAllByRole('spinbutton')
    expect(inputs.map((i) => (i as HTMLInputElement).value)).toEqual(['1', '2', '3', '4', '5', '6', '8', '7'])
    for (const [i, v] of ['10', '20', '30', '40', '50', '60', '80', '70'].entries()) await setNumber(inputs[i], v)
    await userEvent.click(screen.getByText('Сохранить'))
    expect(onSave).toHaveBeenCalledWith({
      rest: { max_requests: 10, max_readings: 20, window_seconds: 30 },
      ws: { max_messages: 40, max_readings: 50, window_seconds: 60 },
      spool_flush_timeout_seconds: 70, ws_max_message_bytes: 80,
    })
  })

  it('flattens nested validation errors and clears them on edit', async () => {
    const onSave = vi.fn()
    render(<TelemetryRateLimitsForm config={null} onSave={onSave} />)
    const inputs = screen.getAllByRole('spinbutton')
    await userEvent.clear(inputs[2])            // rest.window_seconds
    await userEvent.clear(inputs[5])            // ws.window_seconds
    await userEvent.clear(inputs[7])            // spool_flush_timeout_seconds
    await userEvent.click(screen.getByText('Сохранить'))
    expect(onSave).not.toHaveBeenCalled()
    expect(inputs[2]).toHaveAttribute('aria-invalid', 'true')
    expect(inputs[5]).toHaveAttribute('aria-invalid', 'true')
    expect(inputs[7]).toHaveAttribute('aria-invalid', 'true')
    expect(inputs[1]).toHaveAttribute('aria-invalid', 'false')
    await userEvent.type(inputs[2], '1')
    expect(inputs[2]).toHaveAttribute('aria-invalid', 'false')
  })

  it('reflects the saving state', () => {
    render(<TelemetryRateLimitsForm config={null} onSave={vi.fn()} isSaving />)
    expect(screen.getByText('Сохраняю...')).toBeDisabled()
  })
})
