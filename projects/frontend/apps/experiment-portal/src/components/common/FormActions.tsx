import { ReactNode } from 'react'
import './FormActions.scss'

interface FormActionsProps {
    children: ReactNode
}

function FormActions({ children }: FormActionsProps) {
    return <div className="form-actions">{children}</div>
}

export default FormActions

