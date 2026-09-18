import { cleanup, render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, describe, expect, it, vi } from 'vitest'
import PageHeader from './PageHeader'
import EmptyState from './EmptyState'
import Loading from './Loading'
import Error from './Error'
import FormActions from './FormActions'

afterEach(cleanup)

describe('shared primitive adapters', () => {
  it('preserves the header action and empty-state children without a theme provider', async () => {
    const user = userEvent.setup()
    const create = vi.fn()
    render(<>
      <PageHeader title="Проекты" action={<button onClick={create}>Создать</button>} />
      <EmptyState message="Нет проектов"><a href="/help">Помощь</a></EmptyState>
    </>)
    expect(screen.getByRole('heading', { name: 'Проекты' })).toBeInTheDocument()
    expect(screen.getByText('Нет проектов')).toBeInTheDocument()
    await user.tab()
    await user.keyboard('{Enter}')
    expect(create).toHaveBeenCalledTimes(1)
    await user.tab()
    expect(screen.getByRole('link', { name: 'Помощь' })).toHaveFocus()
  })

  it('announces localized loading and errors', () => {
    const { rerender } = render(<Loading showSpinner={false} />)
    expect(screen.getByRole('status')).toHaveTextContent('Загрузка...')
    rerender(<Loading message="Проверка прав доступа..." />)
    expect(screen.getByRole('status')).toHaveTextContent('Проверка прав доступа...')
    rerender(<Error message="Нет доступа" />)
    expect(screen.getByRole('alert')).toHaveTextContent('Нет доступа')
    expect(screen.queryByRole('status')).not.toBeInTheDocument()
  })

  it('keeps cancel, submit and disabled behavior in the consumer form', async () => {
    const user = userEvent.setup()
    const submit = vi.fn((event: React.FormEvent) => event.preventDefault())
    const cancel = vi.fn()
    render(<form onSubmit={submit}>
      <FormActions>
        <button type="button" onClick={cancel}>Отмена</button>
        <button type="submit">Создать проект</button>
        <button type="submit" disabled>Сохранение...</button>
      </FormActions>
    </form>)
    await user.tab()
    await user.keyboard('{Enter}')
    expect(cancel).toHaveBeenCalledTimes(1)
    expect(submit).not.toHaveBeenCalled()
    await user.tab()
    await user.keyboard('{Enter}')
    expect(submit).toHaveBeenCalledTimes(1)
    await user.click(screen.getByRole('button', { name: 'Сохранение...' }))
    expect(submit).toHaveBeenCalledTimes(1)
  })
})
