import { ReactNode } from 'react'
import './MaterialSelect.css'

type MaterialSelectProps = {
    id: string
    label?: string
    value: string | string[]
    onChange: (value: string, event: React.ChangeEvent<HTMLSelectElement>) => void
    children: ReactNode
    disabled?: boolean
    name?: string
    className?: string
    helperText?: string
    required?: boolean
    multiple?: boolean
    size?: number
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
}: MaterialSelectProps) {
    return (
        <div className={`md-select ${className}`.trim()}>
            {label && (
                <label className="md-select__label" htmlFor={id}>
                    {label}
                </label>
            )}
            <div
                className={`md-select__control${disabled ? ' md-select__control--disabled' : ''}${multiple ? ' md-select__control--multiple' : ''
                    }`}
            >
                <select
                    id={id}
                    name={name}
                    className="md-select__field"
                    value={value}
                    onChange={(event) => onChange(event.target.value, event)}
                    disabled={disabled}
                    required={required}
                    multiple={multiple}
                    size={size}
                >
                    {children}
                </select>
                {!multiple && (
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
                )}
            </div>
            {helperText && <div className="md-select__helper">{helperText}</div>}
        </div>
    )
}

export default MaterialSelect
