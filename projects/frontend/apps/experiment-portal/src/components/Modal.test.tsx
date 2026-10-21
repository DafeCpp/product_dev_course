import { describe, it, expect, vi } from 'vitest'
import { render, screen, waitFor } from '@testing-library/react'
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

function AutofocusHarness() {
  const [open, setOpen] = useState(false)
  return (
    <>
      <button onClick={() => setOpen(true)}>Открыть</button>
      <Modal isOpen={open} onClose={() => setOpen(false)} title="Диалог">
        <input aria-label="Поле" autoFocus />
      </Modal>
    </>
  )
}

function PendingHarness() {
  const [open, setOpen] = useState(false)
  const [pending, setPending] = useState(false)
  const submit = () => {
    setOpen(false)
    setPending(true)
    setTimeout(() => setPending(false), 20)
  }
  return (
    <>
      <button disabled={pending} onClick={() => setOpen(true)}>
        Открыть
      </button>
      <Modal isOpen={open} onClose={() => setOpen(false)} title="Диалог">
        <input aria-label="Поле" autoFocus />
        <button onClick={submit}>Отправить</button>
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

  it('restores focus to the trigger when a child autofocuses', async () => {
    const user = userEvent.setup()
    render(<AutofocusHarness />)
    const opener = screen.getByRole('button', { name: 'Открыть' })
    await user.click(opener)
    expect(screen.getByLabelText('Поле')).toHaveFocus()
    await user.keyboard('{Escape}')
    expect(opener).toHaveFocus()
  })

  it('returns focus to a trigger that is disabled while the action runs', async () => {
    const user = userEvent.setup()
    render(<PendingHarness />)
    const opener = screen.getByRole('button', { name: 'Открыть' })
    await user.click(opener)
    await user.click(screen.getByRole('button', { name: 'Отправить' }))
    expect(opener).toBeDisabled()
    await waitFor(() => expect(opener).toBeEnabled())
    expect(opener).toHaveFocus()
  })
})
