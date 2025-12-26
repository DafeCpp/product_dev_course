import './Pagination.css'

interface PaginationProps {
  currentPage: number
  totalPages: number
  onPageChange: (page: number) => void
}

function Pagination({ currentPage, totalPages, onPageChange }: PaginationProps) {
  if (totalPages <= 1) {
    return null
  }

  return (
    <div className="pagination">
      <button
        className="btn btn-secondary"
        onClick={() => onPageChange(Math.max(1, currentPage - 1))}
        disabled={currentPage === 1}
      >
        Назад
      </button>
      <span>
        Страница {currentPage} из {totalPages}
      </span>
      <button
        className="btn btn-secondary"
        onClick={() => onPageChange(Math.min(totalPages, currentPage + 1))}
        disabled={currentPage >= totalPages}
      >
        Вперед
      </button>
    </div>
  )
}

export default Pagination
