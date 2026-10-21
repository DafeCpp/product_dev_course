import { describe, it, expect, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import CaptureSessionTimeline from './CaptureSessionTimeline'
import type { CaptureSession } from '../types'

const session = (over: Partial<CaptureSession>): CaptureSession =>
  ({
    id: 's1',
    run_id: 'r1',
    project_id: 'p1',
    ordinal_number: 1,
    status: 'succeeded',
    started_at: '2026-10-21T10:00:00Z',
    stopped_at: '2026-10-21T10:01:30Z',
    ...over,
  }) as CaptureSession

describe('CaptureSessionTimeline', () => {
  it('exposes each session as a named button that works from the keyboard', async () => {
    const user = userEvent.setup()
    const onSessionClick = vi.fn()
    render(
      <CaptureSessionTimeline
        sessions={[
          session({}),
          session({ id: 's2', ordinal_number: 2, status: 'failed', started_at: '2026-10-21T10:02:00Z', stopped_at: '2026-10-21T10:02:10Z' }),
        ]}
        onSessionClick={onSessionClick}
      />,
    )
    const first = screen.getByRole('button', { name: 'Сессия #1 · Завершён · 1м 30с' })
    const second = screen.getByRole('button', { name: 'Сессия #2 · Ошибка · 10с' })

    first.focus()
    await user.keyboard('{Enter}')
    expect(onSessionClick).toHaveBeenLastCalledWith('s1')

    await user.tab()
    expect(second).toHaveFocus()
    await user.keyboard(' ')
    expect(onSessionClick).toHaveBeenLastCalledWith('s2')
    expect(onSessionClick).toHaveBeenCalledTimes(2)
  })
})
