import './InfoRow.css'

interface InfoRowProps {
  label: string
  value: string | React.ReactNode
  mono?: boolean
}

function InfoRow({ label, value, mono = false }: InfoRowProps) {
  return (
    <div className="info-row">
      <strong>{label}:</strong>
      <span className={mono ? 'mono' : ''}>{value}</span>
    </div>
  )
}

export default InfoRow
