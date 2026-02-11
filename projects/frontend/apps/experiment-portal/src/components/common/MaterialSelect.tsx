import {
    Children,
    CSSProperties,
    ReactElement,
    ReactNode,
    isValidElement,
    useCallback,
    useEffect,
    useMemo,
    useRef,
    useState,
} from 'react'
import { createPortal } from 'react-dom'
import './MaterialSelect.scss'

type MaterialSelectProps = {
    id: string
    label?: string
    value: string | string[]
    onChange: (value: string, event?: React.ChangeEvent<HTMLSelectElement>) => void
    children: ReactNode
    disabled?: boolean
    name?: string
    className?: string
    helperText?: string
    required?: boolean
    multiple?: boolean
    size?: number
    placeholder?: string
}

function MaterialSelect({
    id,
    label,
    value,
    onChange,
    children,
    disabled = false,
    name,
    className = '',
    helperText,
    required = false,
    multiple = false,
    size,
    placeholder = '',
}: MaterialSelectProps) {
    const [isOpen, setIsOpen] = useState(false)
    const menuRef = useRef<HTMLDivElement | null>(null)
    const triggerRef = useRef<HTMLButtonElement | null>(null)
    const [menuStyle, setMenuStyle] = useState<CSSProperties | null>(null)

    const options = useMemo(() => {
        return Children.toArray(children)
            .filter(
                (child): child is ReactElement<{ value?: string; disabled?: boolean; children?: ReactNode }> =>
                    isValidElement(child) && child.type === 'option'
            )
            .map((option) => ({
                value: String(option.props.value ?? ''),
                label: String(option.props.children ?? ''),
                disabled: !!option.props.disabled,
            }))
    }, [children])

    const selectedValue = Array.isArray(value) ? value[0] ?? '' : value
    const selectedLabel =
        options.find((option) => option.value === selectedValue)?.label || ''
    const hasOptions = options.length > 0
    const isDisabled = disabled && hasOptions

    const menuOptions = options
    const listboxId = `${id}__listbox`

    const updateMenuPosition = useCallback(() => {
        if (typeof window === 'undefined') return
        const triggerEl = triggerRef.current
        if (!triggerEl) return

        const rect = triggerEl.getBoundingClientRect()
        const margin = 6
        const viewportPadding = 8
        const maxHeight = 240

        const menuEl = menuRef.current
        const measuredHeight = menuEl ? menuEl.getBoundingClientRect().height : 0
        const menuHeight = Math.min(measuredHeight || maxHeight, maxHeight)

        const spaceBelow = window.innerHeight - rect.bottom
        const spaceAbove = rect.top

        const shouldOpenUp = spaceBelow < menuHeight && spaceAbove > spaceBelow

        let top = shouldOpenUp ? rect.top - margin - menuHeight : rect.bottom + margin

        // Clamp to viewport (avoid rendering off-screen)
        const minTop = viewportPadding
        const maxTop = window.innerHeight - viewportPadding - menuHeight
        top = Math.min(Math.max(top, minTop), Math.max(minTop, maxTop))

        const width = rect.width
        let left = rect.left
        const minLeft = viewportPadding
        const maxLeft = window.innerWidth - viewportPadding - width
        left = Math.min(Math.max(left, minLeft), Math.max(minLeft, maxLeft))

        setMenuStyle({
            position: 'fixed',
            top,
            left,
            width,
            right: 'auto',
            bottom: 'auto',
    // Above ALL modal overlays including nested modals (z-index up to 2000+).
                            zIndex: 10000,
            visibility: 'visible',
            pointerEvents: 'auto',
        })
    }, [])

    useEffect(() => {
        if (!isOpen) return

        const handleClickOutside = (event: MouseEvent) => {
            const target = event.target as Node
            const triggerEl = triggerRef.current
            const menuEl = menuRef.current

            if (!triggerEl) return
            if (triggerEl.contains(target)) return
            if (menuEl?.contains(target)) return

            setIsOpen(false)
        }

        const handleKeyDown = (event: KeyboardEvent) => {
            if (event.key === 'Escape') {
                setIsOpen(false)
            }
        }

        document.addEventListener('mousedown', handleClickOutside)
        document.addEventListener('keydown', handleKeyDown)
        return () => {
            document.removeEventListener('mousedown', handleClickOutside)
            document.removeEventListener('keydown', handleKeyDown)
        }
    }, [isOpen])

    useEffect(() => {
        if (!isOpen) {
            setMenuStyle(null)
            return
        }
        if (typeof window === 'undefined') return

        // Render immediately near the trigger; we'll refine position after mount.
        const triggerEl = triggerRef.current
        const rect = triggerEl?.getBoundingClientRect()
        setMenuStyle({
            position: 'fixed',
            top: rect ? rect.bottom + 6 : 0,
            left: rect ? rect.left : 0,
            width: rect ? rect.width : undefined,
            right: 'auto',
            bottom: 'auto',
            zIndex: 10000,
            visibility: 'visible',
            pointerEvents: 'auto',
        })

        const raf = window.requestAnimationFrame(() => updateMenuPosition())
        const handleScrollOrResize = () => updateMenuPosition()

        window.addEventListener('resize', handleScrollOrResize)
        // Capture scroll on all scroll containers (scroll doesn't bubble).
        window.addEventListener('scroll', handleScrollOrResize, true)

        return () => {
            window.cancelAnimationFrame(raf)
            window.removeEventListener('resize', handleScrollOrResize)
            window.removeEventListener('scroll', handleScrollOrResize, true)
        }
    }, [isOpen, updateMenuPosition])

    const toggleMenu = () => {
        if (isDisabled) return
        setIsOpen((prev) => !prev)
    }

    const handleSelect = (nextValue: string) => {
        if (isDisabled) return
        onChange(nextValue)
        setIsOpen(false)
    }

    const handleTriggerKeyDown = (event: React.KeyboardEvent<HTMLButtonElement>) => {
        if (isDisabled) return
        if (event.key === 'Enter' || event.key === ' ' || event.key === 'ArrowDown') {
            event.preventDefault()
            setIsOpen(true)
        } else if (event.key === 'Escape') {
            setIsOpen(false)
        }
    }
    return (
        <div className={`md-select ${className}`.trim()}>
            {label && (
                <label className="md-select__label" htmlFor={id}>
                    {label}
                </label>
            )}
            <div
                className={`md-select__control${isDisabled ? ' md-select__control--disabled' : ''}${multiple ? ' md-select__control--multiple' : ''
                    }${isOpen ? ' md-select__control--open' : ''}`}
            >
                {multiple ? (
                    <select
                        id={id}
                        name={name}
                        className="md-select__field"
                        value={value}
                        onChange={(event) => onChange(event.target.value, event)}
                        disabled={isDisabled}
                        required={required}
                        multiple={multiple}
                        size={size}
                    >
                        {children}
                    </select>
                ) : (
                    <>
                        <button
                            id={id}
                            ref={triggerRef}
                            type="button"
                            className="md-select__trigger"
                            onClick={toggleMenu}
                            onKeyDown={handleTriggerKeyDown}
                            disabled={isDisabled}
                            aria-haspopup="listbox"
                            aria-expanded={isOpen}
                            aria-controls={listboxId}
                        >
                            <span className={`md-select__value${selectedValue ? '' : ' is-placeholder'}`}>
                                {selectedLabel || placeholder}
                            </span>
                            <svg
                                className="md-select__icon"
                                viewBox="0 0 24 24"
                                aria-hidden="true"
                                focusable="false"
                            >
                                <path
                                    d="M7 10l5 5 5-5"
                                    fill="none"
                                    stroke="currentColor"
                                    strokeWidth="2"
                                    strokeLinecap="round"
                                    strokeLinejoin="round"
                                />
                            </svg>
                        </button>
                        {isOpen &&
                            typeof document !== 'undefined' &&
                            createPortal(
                                <div
                                    ref={menuRef}
                                    id={listboxId}
                                    className="md-select__menu is-open"
                                    style={
                                        menuStyle ?? {
                                            position: 'fixed',
                                            top: 0,
                                            left: 0,
                                            right: 'auto',
                                            bottom: 'auto',
                                            zIndex: 10000,
                                            visibility: 'visible',
                                            pointerEvents: 'auto',
                                        }
                                    }
                                    role="listbox"
                                >
                                    {menuOptions.map((option) => (
                                        <button
                                            key={option.value}
                                            type="button"
                                            className={`md-select__option${option.value === selectedValue ? ' is-selected' : ''
                                                }`}
                                            onClick={() => handleSelect(option.value)}
                                            disabled={option.disabled}
                                            role="option"
                                            aria-selected={option.value === selectedValue}
                                        >
                                            {option.label}
                                        </button>
                                    ))}
                                </div>,
                                document.body
                            )}
                        <select
                            className="md-select__native"
                            name={name}
                            value={selectedValue}
                            onChange={(event) => onChange(event.target.value, event)}
                            required={required}
                            tabIndex={-1}
                            aria-hidden="true"
                        >
                            {children}
                        </select>
                    </>
                )}
            </div>
            {helperText && <div className="md-select__helper">{helperText}</div>}
        </div>
    )
}

export default MaterialSelect
