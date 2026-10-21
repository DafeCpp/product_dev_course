import { describe, it, expect, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { useState } from 'react'
import Modal from './Modal'

function Harness() {
  const [open, setOpen] = useState(false)
  return (
    <>
      <button onClick={() => setOpen(true)}>Открыть</button>
      <button>Снаружи</button>
      <Modal isOpen={open} onClose={() => setOpen(false)} title="Диалог">
        <input aria-label="Поле" />
        <button>Последняя</button>
      </Modal>
    </>
  )
}

describe('Modal', () => {
  it('is announced as a modal dialog labelled by its title', async () => {
    render(<Modal isOpen onClose={vi.fn()} title="Диалог">тело</Modal>)
    const dialog = screen.getByRole('dialog', { name: 'Диалог' })
    expect(dialog).toHaveAttribute('aria-modal', 'true')
    expect(dialog).toHaveFocus()
  })

  it('keeps Tab inside the dialog and restores focus on close', async () => {
    const user = userEvent.setup()
    render(<Harness />)
    const opener = screen.getByRole('button', { name: 'Открыть' })
    await user.click(opener)

    await user.tab()
    expect(screen.getByRole('button', { name: '×' })).toHaveFocus()
    await user.tab()
    expect(screen.getByLabelText('Поле')).toHaveFocus()
    await user.tab()
    expect(screen.getByRole('button', { name: 'Последняя' })).toHaveFocus()
    await user.tab()
    expect(screen.getByRole('button', { name: '×' })).toHaveFocus()
    await user.tab({ shift: true })
    expect(screen.getByRole('button', { name: 'Последняя' })).toHaveFocus()

    await user.keyboard('{Escape}')
    expect(screen.queryByRole('dialog')).not.toBeInTheDocument()
    expect(opener).toHaveFocus()
  })
})
