import { describe, it, expect, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import PromptDialog from './PromptDialog'

function renderDialog(props: Partial<React.ComponentProps<typeof PromptDialog>> = {}) {
  const onSubmit = vi.fn()
  const onCancel = vi.fn()
  const utils = render(
    <PromptDialog
      isOpen
      title="Заголовок"
      label="Значение"
      onSubmit={onSubmit}
      onCancel={onCancel}
      {...props}
    />,
  )
  return { ...utils, onSubmit, onCancel }
}

describe('PromptDialog', () => {
  it('renders nothing when closed', () => {
    renderDialog({ isOpen: false })
    expect(screen.queryByLabelText('Значение')).not.toBeInTheDocument()
  })

  it('focuses the field and submits the typed value on Enter', async () => {
    const user = userEvent.setup()
    const { onSubmit } = renderDialog()
    expect(screen.getByLabelText('Значение')).toHaveFocus()
    await user.keyboard('hello{Enter}')
    expect(onSubmit).toHaveBeenCalledWith('hello')
  })

  it('submits from a multiline field with the submit button', async () => {
    const user = userEvent.setup()
    const { onSubmit } = renderDialog({ multiline: true, submitLabel: 'Готово' })
    await user.type(screen.getByLabelText('Значение'), 'a{Enter}b')
    await user.click(screen.getByRole('button', { name: 'Готово' }))
    expect(onSubmit).toHaveBeenCalledWith('a\nb')
  })

  it('cancels via the button and via Escape', async () => {
    const user = userEvent.setup()
    const { onCancel, onSubmit } = renderDialog()
    await user.click(screen.getByRole('button', { name: 'Отмена' }))
    await user.keyboard('{Escape}')
    expect(onCancel).toHaveBeenCalledTimes(2)
    expect(onSubmit).not.toHaveBeenCalled()
  })

  it('shows the validation error and keeps the dialog open', async () => {
    const user = userEvent.setup()
    const { onSubmit } = renderDialog({
      validate: (v) => (v === 'ok' ? null : 'Плохое значение'),
    })
    await user.keyboard('bad{Enter}')
    expect(screen.getByRole('alert')).toHaveTextContent('Плохое значение')
    expect(screen.getByLabelText('Значение')).toHaveAttribute('aria-invalid', 'true')
    expect(onSubmit).not.toHaveBeenCalled()
    await user.clear(screen.getByLabelText('Значение'))
    await user.keyboard('ok{Enter}')
    expect(onSubmit).toHaveBeenCalledWith('ok')
  })

  it('starts from an empty field each time it opens', async () => {
    const user = userEvent.setup()
    const { rerender, onSubmit, onCancel } = renderDialog()
    await user.keyboard('draft')
    const props = { title: 'Заголовок', label: 'Значение', onSubmit, onCancel }
    rerender(<PromptDialog isOpen={false} {...props} />)
    rerender(<PromptDialog isOpen {...props} />)
    expect(screen.getByLabelText('Значение')).toHaveValue('')
  })
})
