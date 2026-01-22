import { ReactNode } from 'react'
import './InfoRow.scss'

interface InfoRowProps {
    label: string
    value: ReactNode
    className?: string
}

function InfoRow({ label, value, className = '' }: InfoRowProps) {
    return (
        <div className={`info-row ${className}`}>
            <strong>{label}:</strong>
            <span>{value}</span>
        </div>
    )
}

export default InfoRow

