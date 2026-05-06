import type { Script } from '../../types/scripts'
import PermissionGate from '../../components/PermissionGate'

export interface ScriptsTableProps {
  scripts: Script[]
  toggleActivePending: boolean
  deletePending: boolean
  onEdit: (script: Script) => void
  onToggleActive: (script: Script) => void
  onDelete: (script: Script) => void
}

export default function ScriptsTable({
  scripts,
  toggleActivePending,
  deletePending,
  onEdit,
  onToggleActive,
  onDelete,
}: ScriptsTableProps) {
  return (
    <div className="scripts-table-wrap card">
      <table className="scripts-table">
        <thead>
          <tr>
            <th>Название</th>
            <th>Описание</th>
            <th>Сервис</th>
            <th>Тип</th>
            <th>Таймаут</th>
            <th>Активен</th>
            <th>Действия</th>
          </tr>
        </thead>
        <tbody>
          {scripts.map((s) => (
            <tr key={s.id}>
              <td className="scripts-table__name">{s.name}</td>
              <td className="scripts-table__desc">{s.description ?? '—'}</td>
              <td>
                <span className="scripts-table__service">{s.target_service}</span>
              </td>
              <td>
                <span className="scripts-table__type">{s.script_type}</span>
              </td>
              <td>{s.timeout_sec} с</td>
              <td>
                <span
                  className={`scripts-table__active ${s.is_active ? 'scripts-table__active--yes' : 'scripts-table__active--no'}`}
                >
                  {s.is_active ? 'Да' : 'Нет'}
                </span>
              </td>
              <td className="scripts-table__actions">
                <PermissionGate permission="scripts.manage" system>
                  <button
                    className="btn btn-secondary btn-sm"
                    onClick={() => onEdit(s)}
                  >
                    Редактировать
                  </button>
                  <button
                    className="btn btn-secondary btn-sm"
                    onClick={() => onToggleActive(s)}
                    disabled={toggleActivePending}
                  >
                    {s.is_active ? 'Деактивировать' : 'Активировать'}
                  </button>
                  <button
                    className="btn btn-danger btn-sm"
                    onClick={() => onDelete(s)}
                    disabled={deletePending}
                  >
                    Удалить
                  </button>
                </PermissionGate>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}
