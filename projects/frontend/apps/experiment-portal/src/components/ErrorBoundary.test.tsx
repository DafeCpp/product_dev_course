import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { MemoryRouter, Route, Routes } from 'react-router-dom'
import ErrorBoundary, { isChunkLoadError } from './ErrorBoundary'

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

  it('recovers via the back link even when it points at the current page', async () => {
    const user = userEvent.setup()
    render(
      <MemoryRouter initialEntries={['/experiments']}>
        <ErrorBoundary resetKey="/experiments">
          <Routes>
            <Route path="/experiments" element={<Boom />} />
          </Routes>
        </ErrorBoundary>
      </MemoryRouter>,
    )
    shouldThrow = false
    await user.click(screen.getByRole('link', { name: 'К экспериментам' }))
    expect(screen.getByText('Страница работает')).toBeInTheDocument()
  })

  it('reloads the page on retry when a page chunk failed to load', async () => {
    const user = userEvent.setup()
    const reload = vi.fn()
    vi.spyOn(window, 'location', 'get').mockReturnValue({ ...window.location, reload })
    function ChunkFail(): never {
      throw new TypeError('Failed to fetch dynamically imported module: /assets/RunDetail.js')
    }
    render(
      <MemoryRouter>
        <ErrorBoundary>
          <ChunkFail />
        </ErrorBoundary>
      </MemoryRouter>,
    )
    await user.click(screen.getByRole('button', { name: 'Повторить' }))
    expect(reload).toHaveBeenCalledTimes(1)
  })

  it('recognises chunk load errors', () => {
    expect(isChunkLoadError(new TypeError('Failed to fetch dynamically imported module: /a.js'))).toBe(true)
    expect(isChunkLoadError(new TypeError('Importing a module script failed.'))).toBe(true)
    expect(isChunkLoadError(new Error('render exploded'))).toBe(false)
  })
})
