import { ReactNode } from 'react'
import './FormGroup.scss'

interface FormGroupProps {
    label: string
    htmlFor?: string
    required?: boolean
    hint?: string
    children: ReactNode
}

function FormGroup({ label, htmlFor, required, hint, children }: FormGroupProps) {
    return (
        <div className="form-group">
            <label htmlFor={htmlFor}>
                {label}
                {required && <span className="required">*</span>}
            </label>
            {children}
            {hint && <small className="form-hint">{hint}</small>}
        </div>
    )
}

export default FormGroup

