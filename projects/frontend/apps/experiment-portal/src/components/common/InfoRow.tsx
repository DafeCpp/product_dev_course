import { ReactNode } from 'react'
import './InfoRow.css'

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

