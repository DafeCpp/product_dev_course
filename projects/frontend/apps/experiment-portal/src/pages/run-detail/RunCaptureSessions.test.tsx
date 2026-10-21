import { describe, it, expect, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import RunCaptureSessions, { SESSIONS_PAGE_SIZE } from './RunCaptureSessions'
import { createMockCaptureSession } from '../../testUtils/factories'

vi.mock('../../components/AuditLog', () => ({ default: () => null }))

const mutation = { mutate: vi.fn(), isPending: false }

function renderSessions(count: number) {
  const sessions = Array.from({ length: count }, (_, i) =>
    createMockCaptureSession({ id: `s${i + 1}`, ordinal_number: i + 1, status: 'succeeded' })
  )
  render(
    <RunCaptureSessions
      runId="run-1"
      sessions={sessions}
      activeSession={undefined}
      sessionsLoading={false}
      canManageSessions
      experiment={undefined}
      createSessionMutation={mutation}
      onCreateSession={vi.fn()}
      stopSessionMutation={mutation}
      deleteSessionMutation={mutation}
      setExportTarget={vi.fn()}
    />
  )
}

const cardTitles = () => screen.queryAllByRole('heading', { level: 4 }).map((h) => h.textContent)

describe('RunCaptureSessions paging', () => {
  it('renders finished sessions a page at a time, newest first', async () => {
    const user = userEvent.setup()
    renderSessions(45)

    expect(cardTitles()).toHaveLength(SESSIONS_PAGE_SIZE)
    expect(cardTitles()[0]).toBe('Сессия #45')
    expect(cardTitles()[SESSIONS_PAGE_SIZE - 1]).toBe('Сессия #26')

    await user.click(screen.getByRole('button', { name: 'Показать ещё 20 из 25' }))
    expect(cardTitles()).toHaveLength(40)

    await user.click(screen.getByRole('button', { name: 'Показать ещё 5 из 5' }))
    expect(cardTitles()).toHaveLength(45)
    expect(cardTitles()[44]).toBe('Сессия #1')
    expect(screen.queryByRole('button', { name: /Показать ещё/ })).not.toBeInTheDocument()
  })

  it('shows no pager when everything fits on one page', () => {
    renderSessions(SESSIONS_PAGE_SIZE)
    expect(cardTitles()).toHaveLength(SESSIONS_PAGE_SIZE)
    expect(screen.queryByRole('button', { name: /Показать ещё/ })).not.toBeInTheDocument()
  })
})
