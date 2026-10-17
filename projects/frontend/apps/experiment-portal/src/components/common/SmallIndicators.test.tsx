import { describe, it, expect, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import LiveSwitch from './LiveSwitch'
import CircleXIndicator from './CircleXIndicator'
import ConnectionStatusIndicator from './ConnectionStatusIndicator'

describe('LiveSwitch', () => {
  it('toggles by click, Enter and Space and shows labels', async () => {
    const onChange = vi.fn()
    const { rerender } = render(<LiveSwitch live={false} onChange={onChange} />)
    const sw = screen.getByRole('switch')
    expect(sw).toHaveAttribute('aria-checked', 'false')
    expect(screen.getByText('History')).toBeInTheDocument()
    await userEvent.click(sw)
    expect(onChange).toHaveBeenLastCalledWith(true)
    sw.focus()
    await userEvent.keyboard('{Enter}')
    await userEvent.keyboard(' ')
    await userEvent.keyboard('a')
    expect(onChange).toHaveBeenCalledTimes(3)
    rerender(<LiveSwitch live onChange={onChange} labelOn="Онлайн" />)
    expect(screen.getByText('Онлайн')).toBeInTheDocument()
    expect(screen.getByRole('switch')).toHaveAttribute('aria-checked', 'true')
  })
})

describe('CircleXIndicator', () => {
  it('uses the default, title and explicit aria labels', () => {
    const { rerender } = render(<CircleXIndicator />)
    expect(screen.getByRole('img')).toHaveAttribute('aria-label', 'Недоступно')
    rerender(<CircleXIndicator title="нет" variant="fab" className="x" />)
    const el = screen.getByRole('img')
    expect(el).toHaveAttribute('aria-label', 'нет')
    expect(el).toHaveClass('circle-x--fab', 'x')
    rerender(<CircleXIndicator title="нет" ariaLabel="метка" />)
    expect(screen.getByRole('img')).toHaveAttribute('aria-label', 'метка')
  })
})

describe('ConnectionStatusIndicator', () => {
  it('renders nothing without a status', () => {
    const { container } = render(<ConnectionStatusIndicator status={undefined} />)
    expect(container).toBeEmptyDOMElement()
  })
  it.each([
    ['online', 'Онлайн'],
    ['delayed', 'Задержка'],
    ['offline', 'Оффлайн'],
  ] as const)('shows %s', (status, label) => {
    render(<ConnectionStatusIndicator status={status} showLabel />)
    expect(screen.getByText(label)).toBeInTheDocument()
    expect(screen.getByTitle(label)).toHaveClass(`conn-indicator--${status}`)
  })
  it('hides the label by default', () => {
    render(<ConnectionStatusIndicator status="online" />)
    expect(screen.queryByText('Онлайн')).toBeNull()
  })
})
