import { describe, it, expect, vi, afterEach } from 'vitest'
import { act, fireEvent, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { useState } from 'react'
import MaterialSelect from './MaterialSelect'
import { pickMaterialSelectOption } from '../../testUtils/materialSelect'

function Controlled(props: { initial?: string; onChange?: (v: string) => void; disabled?: boolean }) {
    const [value, setValue] = useState(props.initial ?? '')
    return (
        <MaterialSelect
            id="fruit"
            label="Fruit"
            value={value}
            placeholder="Pick one"
            helperText="Help text"
            disabled={props.disabled}
            onChange={(v) => {
                setValue(v)
                props.onChange?.(v)
            }}
        >
            <option>None</option>
            <option value="apple">Apple</option>
            <option value="pear" disabled>
                Pear
            </option>
            {'stray text'}
            <></>
            <option value="kiwi">{undefined}</option>
        </MaterialSelect>
    )
}

const rect = (r: Partial<DOMRect>): DOMRect =>
    ({ top: 0, bottom: 0, left: 0, right: 0, width: 0, height: 0, x: 0, y: 0, toJSON: () => ({}), ...r }) as DOMRect

afterEach(() => {
    vi.restoreAllMocks()
})

describe('MaterialSelect (single)', () => {
    it('renders label, placeholder, helper and opens/selects via click', async () => {
        const user = userEvent.setup()
        const onChange = vi.fn()
        render(<Controlled onChange={onChange} />)

        const trigger = screen.getByLabelText('Fruit')
        expect(trigger).toHaveTextContent('None')
        expect(screen.getByText('Help text')).toBeInTheDocument()
        expect(trigger).toHaveAttribute('aria-expanded', 'false')

        await user.click(trigger)
        expect(trigger).toHaveAttribute('aria-expanded', 'true')
        const listbox = screen.getByRole('listbox')
        expect(listbox.style.position).toBe('fixed')
        expect(screen.getByRole('option', { name: 'Pear' })).toBeDisabled()

        await user.click(screen.getByRole('option', { name: 'Apple' }))
        expect(onChange).toHaveBeenCalledWith('apple')
        expect(screen.queryByRole('listbox')).not.toBeInTheDocument()
        expect(trigger).toHaveTextContent('Apple')

        await user.click(trigger)
        expect(screen.getByRole('option', { name: 'Apple' })).toHaveAttribute('aria-selected', 'true')
        // toggle closed via click on trigger
        await user.click(trigger)
        expect(screen.queryByRole('listbox')).not.toBeInTheDocument()
    })

    it('works with the pickMaterialSelectOption helper', async () => {
        const user = userEvent.setup()
        const onChange = vi.fn()
        render(<Controlled onChange={onChange} />)
        await pickMaterialSelectOption(user, 'Fruit', 'Apple')
        expect(onChange).toHaveBeenCalledWith('apple')
    })

    it('helper falls back to role=listbox when aria-controls is missing', async () => {
        const user = userEvent.setup()
        const onChange = vi.fn()
        render(<Controlled onChange={onChange} />)
        screen.getByLabelText('Fruit').removeAttribute('aria-controls')
        await pickMaterialSelectOption(user, 'Fruit', 'Apple')
        expect(onChange).toHaveBeenCalledWith('apple')
    })

    it('helper rejects when the referenced listbox never appears', async () => {
        const user = userEvent.setup()
        render(
            <button type="button" aria-label="Ghost" aria-controls="missing-listbox">
                g
            </button>
        )
        await expect(pickMaterialSelectOption(user, 'Ghost', 'x')).rejects.toThrow('MaterialSelect listbox not found')
    })

    it('opens with keyboard keys and closes with Escape on trigger and document', async () => {
        const user = userEvent.setup()
        render(<Controlled />)
        const trigger = screen.getByLabelText('Fruit')

        fireEvent.keyDown(trigger, { key: 'Enter' })
        expect(screen.getByRole('listbox')).toBeInTheDocument()
        fireEvent.keyDown(trigger, { key: 'Escape' })
        expect(screen.queryByRole('listbox')).not.toBeInTheDocument()

        fireEvent.keyDown(trigger, { key: ' ' })
        expect(screen.getByRole('listbox')).toBeInTheDocument()
        fireEvent.keyDown(document, { key: 'a' })
        expect(screen.getByRole('listbox')).toBeInTheDocument()
        fireEvent.keyDown(document, { key: 'Escape' })
        expect(screen.queryByRole('listbox')).not.toBeInTheDocument()

        fireEvent.keyDown(trigger, { key: 'ArrowDown' })
        expect(screen.getByRole('listbox')).toBeInTheDocument()
        fireEvent.keyDown(trigger, { key: 'Tab' })
        expect(screen.getByRole('listbox')).toBeInTheDocument()
        await user.keyboard('{Escape}')
        expect(screen.queryByRole('listbox')).not.toBeInTheDocument()
    })

    it('closes on outside mousedown but not on mousedown inside trigger/menu', async () => {
        const user = userEvent.setup()
        render(
            <div>
                <Controlled />
                <p>outside</p>
            </div>
        )
        const trigger = screen.getByLabelText('Fruit')
        await user.click(trigger)

        fireEvent.mouseDown(trigger)
        expect(screen.getByRole('listbox')).toBeInTheDocument()
        fireEvent.mouseDown(screen.getByRole('option', { name: 'Apple' }))
        expect(screen.getByRole('listbox')).toBeInTheDocument()
        fireEvent.mouseDown(screen.getByText('outside'))
        expect(screen.queryByRole('listbox')).not.toBeInTheDocument()
    })

    it('ignores interaction when disabled with options', async () => {
        render(<Controlled disabled />)
        const trigger = screen.getByLabelText('Fruit')
        expect(trigger).toBeDisabled()
        expect(trigger.closest('.md-select__control')).toHaveClass('md-select__control--disabled')
        fireEvent.keyDown(trigger, { key: 'Enter' })
        fireEvent.click(trigger)
        expect(screen.queryByRole('listbox')).not.toBeInTheDocument()
    })

    it('is not disabled when there are no options; native select forwards changes', () => {
        const onChange = vi.fn()
        const { container } = render(
            <MaterialSelect id="empty" value={[]} onChange={onChange} disabled placeholder="Pick one" name="n" className="extra">
                {null}
            </MaterialSelect>
        )
        const trigger = container.querySelector('button#empty') as HTMLButtonElement
        expect(trigger).not.toBeDisabled()
        expect(container.querySelector('.md-select')).toHaveClass('extra')
        expect(container.querySelector('label')).toBeNull()
        expect(container.querySelector('.md-select__value')).toHaveClass('is-placeholder')
        expect(trigger).toHaveTextContent('Pick one')
        const native = container.querySelector('select.md-select__native') as HTMLSelectElement
        fireEvent.change(native, { target: { value: '' } })
        expect(onChange).toHaveBeenCalledWith('', expect.anything())
    })

    it('uses first array value as selection', () => {
        render(
            <MaterialSelect id="arr" label="Arr" value={['b', 'a']} onChange={vi.fn()}>
                <option value="a">A</option>
                <option value="b">B</option>
            </MaterialSelect>
        )
        expect(screen.getByLabelText('Arr')).toHaveTextContent('B')
    })

    it('positions menu: opens up when no space below, clamps, and repositions on resize/scroll', async () => {
        const user = userEvent.setup()
        const rafCallbacks: FrameRequestCallback[] = []
        vi.spyOn(window, 'requestAnimationFrame').mockImplementation((cb) => {
            rafCallbacks.push(cb)
            return rafCallbacks.length
        })
        const cancelSpy = vi.spyOn(window, 'cancelAnimationFrame').mockImplementation(() => undefined)

        render(<Controlled />)
        const trigger = screen.getByLabelText('Fruit')
        const triggerRect = vi
            .spyOn(trigger, 'getBoundingClientRect')
            .mockReturnValue(rect({ top: 700, bottom: 740, left: 5000, width: 200 }))

        await user.click(trigger)
        const listbox = screen.getByRole('listbox')
        // initial placement directly from trigger rect
        expect(listbox.style.top).toBe('746px')
        expect(listbox.style.left).toBe('5000px')

        vi.spyOn(listbox, 'getBoundingClientRect').mockReturnValue(rect({ height: 100 }))
        await act(async () => {
            rafCallbacks.forEach((cb) => cb(0))
        })
        // innerHeight 768 in jsdom: spaceBelow 28 < 100, spaceAbove 700 => open up: 700 - 6 - 100
        expect(listbox.style.top).toBe('594px')
        // left clamped to innerWidth - 8 - width
        expect(listbox.style.left).toBe(`${window.innerWidth - 8 - 200}px`)

        // measured height 0 → maxHeight 240; trigger near top → open down, clamp top
        triggerRect.mockReturnValue(rect({ top: -50, bottom: -10, left: -20, width: 100 }))
        vi.spyOn(listbox, 'getBoundingClientRect').mockReturnValue(rect({ height: 0 }))
        await act(async () => {
            window.dispatchEvent(new Event('resize'))
        })
        expect(listbox.style.top).toBe('8px')
        expect(listbox.style.left).toBe('8px')

        triggerRect.mockReturnValue(rect({ top: 100, bottom: 140, left: 30, width: 100 }))
        await act(async () => {
            window.dispatchEvent(new Event('scroll'))
        })
        expect(listbox.style.top).toBe('146px')
        expect(listbox.style.left).toBe('30px')

        await user.click(trigger)
        await waitFor(() => expect(screen.queryByRole('listbox')).not.toBeInTheDocument())
        expect(cancelSpy).toHaveBeenCalled()
    })
})

describe('MaterialSelect (pill variant)', () => {
    it('renders icon, aria-label and floating label', async () => {
        const user = userEvent.setup()
        const { container, rerender } = render(
            <MaterialSelect id="p" label="Status" value="" onChange={vi.fn()} variant="pill" icon={<i>ic</i>}>
                <option value="a">Active</option>
            </MaterialSelect>
        )
        expect(container.querySelector('.md-select')).toHaveClass('md-select--pill', 'md-select--has-icon')
        expect(container.querySelector('label')).toBeNull()
        const trigger = screen.getByRole('button', { name: 'Status' })
        expect(trigger).toHaveClass('md-select__trigger--pill')
        expect(screen.getByText('ic')).toBeInTheDocument()
        const floatLabel = container.querySelector('.md-select__float-label')!
        expect(floatLabel).not.toHaveClass('is-floating')
        expect(container.querySelector('.md-select__value')).toHaveTextContent('')

        await user.click(trigger)
        expect(floatLabel).toHaveClass('is-floating')

        rerender(
            <MaterialSelect id="p" value="a" onChange={vi.fn()} variant="pill">
                <option value="a">Active</option>
            </MaterialSelect>
        )
        expect(container.querySelector('.md-select')).not.toHaveClass('md-select--has-icon')
        expect(container.querySelector('.md-select__float-label')).toBeNull()
        expect(container.querySelector('.md-select__value')).toHaveTextContent('Active')
        expect(container.querySelector('button#p')).not.toHaveAttribute('aria-label')
    })
})

describe('MaterialSelect (multiple)', () => {
    it('renders a native multi-select and forwards changes', async () => {
        const user = userEvent.setup()
        const onChange = vi.fn()
        const { container } = render(
            <MaterialSelect id="m" label="Tags" value={['a']} onChange={onChange} multiple size={3} required>
                <option value="a">A</option>
                <option value="b">B</option>
            </MaterialSelect>
        )
        expect(container.querySelector('.md-select__control')).toHaveClass('md-select__control--multiple')
        const select = screen.getByLabelText('Tags')
        expect(select).toBeInstanceOf(HTMLSelectElement)
        await pickMaterialSelectOption(user, 'Tags', 'B')
        expect(onChange).toHaveBeenCalledWith(expect.any(String), expect.anything())
    })
})
