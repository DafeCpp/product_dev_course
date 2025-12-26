import './MetadataSection.css'

interface MetadataSectionProps {
  metadata: Record<string, any>
  title?: string
  className?: string
}

function MetadataSection({
  metadata,
  title = 'Метаданные',
  className = 'metadata-section',
}: MetadataSectionProps) {
  if (!metadata || Object.keys(metadata).length === 0) {
    return null
  }

  return (
    <div className={className}>
      <h3>{title}</h3>
      <pre className="metadata-json">
        {JSON.stringify(metadata, null, 2)}
      </pre>
    </div>
  )
}

export default MetadataSection
