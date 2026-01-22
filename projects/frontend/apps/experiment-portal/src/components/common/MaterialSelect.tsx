import {
    Children,
    ReactElement,
    ReactNode,
    isValidElement,
    useEffect,
    useMemo,
    useRef,
    useState,
} from 'react'
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

    useEffect(() => {
        if (!isOpen) return

        const handleClickOutside = (event: MouseEvent) => {
            if (
                menuRef.current &&
                !menuRef.current.contains(event.target as Node) &&
                triggerRef.current &&
                !triggerRef.current.contains(event.target as Node)
            ) {
                setIsOpen(false)
            }
        }

        document.addEventListener('mousedown', handleClickOutside)
        return () => document.removeEventListener('mousedown', handleClickOutside)
    }, [isOpen])

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
                        <div
                            ref={menuRef}
                            className={`md-select__menu${isOpen ? ' is-open' : ''}`}
                            role="listbox"
                            aria-hidden={!isOpen}
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
                        </div>
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
