import './Tags.css'

interface TagsProps {
  tags: string[]
}

function Tags({ tags }: TagsProps) {
  if (!tags || tags.length === 0) {
    return null
  }

  return (
    <div className="tags">
      {tags.map((tag) => (
        <span key={tag} className="tag">
          {tag}
        </span>
      ))}
    </div>
  )
}

export default Tags
