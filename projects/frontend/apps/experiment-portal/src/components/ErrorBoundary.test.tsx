import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { MemoryRouter } from 'react-router-dom'
import ErrorBoundary from './ErrorBoundary'

let shouldThrow = true

function Boom() {
  if (shouldThrow) throw new Error('render exploded')
  return <div>Страница работает</div>
}

function renderBoundary(resetKey = '/a') {
  return render(
    <MemoryRouter>
      <ErrorBoundary resetKey={resetKey}>
        <Boom />
      </ErrorBoundary>
    </MemoryRouter>,
  )
}

describe('ErrorBoundary', () => {
  beforeEach(() => {
    shouldThrow = true
    // React logs caught render errors; keep the test output clean.
    vi.spyOn(console, 'error').mockImplementation(() => {})
  })
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('shows a fallback with the error and a way back', () => {
    renderBoundary()
    expect(screen.getByText('Что-то пошло не так')).toBeInTheDocument()
    expect(screen.getByText('render exploded')).toBeInTheDocument()
    expect(screen.getByRole('link', { name: 'К экспериментам' })).toHaveAttribute('href', '/experiments')
  })

  it('renders the children again after retry', async () => {
    const user = userEvent.setup()
    renderBoundary()
    shouldThrow = false
    await user.click(screen.getByRole('button', { name: 'Повторить' }))
    expect(screen.getByText('Страница работает')).toBeInTheDocument()
  })

  it('clears the error when the reset key changes', () => {
    const { rerender } = renderBoundary('/a')
    shouldThrow = false
    rerender(
      <MemoryRouter>
        <ErrorBoundary resetKey="/b">
          <Boom />
        </ErrorBoundary>
      </MemoryRouter>,
    )
    expect(screen.getByText('Страница работает')).toBeInTheDocument()
  })
})
