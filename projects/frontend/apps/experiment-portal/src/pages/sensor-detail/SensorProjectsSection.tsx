import type { Sensor, Project } from '../../types'
import { Loading, MaterialSelect } from '../../components/common'

interface SensorProjectsMutation<TVar = void> {
  mutate: (variables: TVar) => void
  isPending: boolean
  isError: boolean
  error: unknown
}

interface SensorProjectsSectionProps {
  sensor: Sensor
  sensorProjectsData: { project_ids: string[] } | undefined
  isLoadingProjects: boolean
  allProjectsData: { projects: Project[] } | undefined
  showAddProjectModal: boolean
  selectedProjectId: string
  addProjectMutation: SensorProjectsMutation<string>
  removeProjectMutation: SensorProjectsMutation<string>
  setShowAddProjectModal: (show: boolean) => void
  setSelectedProjectId: (id: string) => void
}

export default function SensorProjectsSection({
  sensor,
  sensorProjectsData,
  isLoadingProjects,
  allProjectsData,
  showAddProjectModal,
  selectedProjectId,
  addProjectMutation,
  removeProjectMutation,
  setShowAddProjectModal,
  setSelectedProjectId,
}: SensorProjectsSectionProps) {
  return (
    <div className="sensor-projects-section">
      <div className="section-header">
        <h3>Проекты датчика</h3>
        <button
          className="btn btn-primary btn-sm"
          onClick={() => setShowAddProjectModal(true)}
          disabled={addProjectMutation.isPending || removeProjectMutation.isPending}
        >
          Добавить проект
        </button>
      </div>

      {isLoadingProjects && <Loading />}

      {!isLoadingProjects && sensorProjectsData && (
        <>
          {sensorProjectsData.project_ids.length === 0 ? (
            <p className="text-muted">Датчик не привязан ни к одному проекту</p>
          ) : (
            <div className="projects-list">
              <table>
                <thead>
                  <tr>
                    <th>ID проекта</th>
                    <th>Название</th>
                    <th>Действия</th>
                  </tr>
                </thead>
                <tbody>
                  {sensorProjectsData.project_ids.map((projectId) => {
                    const project = allProjectsData?.projects.find((p) => p.id === projectId)
                    const isPrimary = projectId === sensor.project_id

                    return (
                      <tr key={projectId}>
                        <td>
                          <span className="mono">{projectId}</span>
                          {isPrimary && (
                            <span className="badge badge-primary" style={{ marginLeft: '8px' }}>
                              Основной
                            </span>
                          )}
                        </td>
                        <td>{project?.name || 'Неизвестный проект'}</td>
                        <td>
                          {!isPrimary && (
                            <button
                              className="btn btn-danger btn-sm"
                              onClick={() => {
                                if (
                                  confirm(
                                    `Удалить датчик из проекта ${project?.name || projectId}?`
                                  )
                                ) {
                                  removeProjectMutation.mutate(projectId)
                                }
                              }}
                              disabled={removeProjectMutation.isPending}
                            >
                              Удалить
                            </button>
                          )}
                          {isPrimary && (
                            <span className="text-muted">Нельзя удалить основной проект</span>
                          )}
                        </td>
                      </tr>
                    )
                  })}
                </tbody>
              </table>
            </div>
          )}
        </>
      )}

      {/* Модальное окно для добавления проекта */}
      {showAddProjectModal && (
        <div className="modal-overlay" onClick={() => setShowAddProjectModal(false)}>
          <div className="modal-content" onClick={(e) => e.stopPropagation()}>
            <div className="modal-header">
              <h2>Добавить проект</h2>
              <button
                type="button"
                className="modal-close"
                onClick={() => {
                  setShowAddProjectModal(false)
                  setSelectedProjectId('')
                }}
                disabled={addProjectMutation.isPending}
              >
                ×
              </button>
            </div>
            <div className="modal-form">
              <div className="form-group">
                <label htmlFor="add-project-select">
                  Проект <span className="required">*</span>
                </label>
                <MaterialSelect
                  id="add-project-select"
                  value={selectedProjectId}
                  onChange={(value) => setSelectedProjectId(value)}
                  disabled={addProjectMutation.isPending}
                >
                  <option value="">Выберите проект</option>
                  {allProjectsData?.projects
                    .filter((p) => !sensorProjectsData?.project_ids.includes(p.id))
                    .map((project) => (
                      <option key={project.id} value={project.id}>
                        {project.name}
                      </option>
                    ))}
                </MaterialSelect>
              </div>
              {allProjectsData?.projects.filter(
                (p) => !sensorProjectsData?.project_ids.includes(p.id)
              ).length === 0 && (
                <p className="text-muted">
                  Все доступные проекты уже добавлены к датчику
                </p>
              )}
              {addProjectMutation.isError && (
                <div className="error">
                  {addProjectMutation.error &&
                    typeof addProjectMutation.error === 'object' &&
                    'message' in addProjectMutation.error
                    ? String((addProjectMutation.error as { message: unknown }).message)
                    : 'Ошибка при добавлении проекта'}
                </div>
              )}
              <div className="modal-actions">
                <button
                  type="button"
                  className="btn btn-secondary"
                  onClick={() => {
                    setShowAddProjectModal(false)
                    setSelectedProjectId('')
                  }}
                  disabled={addProjectMutation.isPending}
                >
                  Отмена
                </button>
                <button
                  type="button"
                  className="btn btn-primary"
                  onClick={() => {
                    if (selectedProjectId) {
                      addProjectMutation.mutate(selectedProjectId)
                    }
                  }}
                  disabled={!selectedProjectId || addProjectMutation.isPending}
                >
                  {addProjectMutation.isPending ? 'Добавление...' : 'Добавить'}
                </button>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}
