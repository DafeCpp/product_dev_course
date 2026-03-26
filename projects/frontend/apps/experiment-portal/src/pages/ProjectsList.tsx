import { useState, useEffect, useRef } from 'react'
import { createPortal } from 'react-dom'
import { useQuery } from '@tanstack/react-query'
import { projectsApi } from '../api/client'
import { authApi } from '../api/auth'
import { Loading, Error, EmptyState, FloatingActionButton } from '../components/common'
import ProjectModal from '../components/ProjectModal'
import ProjectMembersModal from '../components/ProjectMembersModal'
import './ProjectsList.scss'

const ROLE_OPTIONS = [
  { value: '', label: 'Все роли' },
  { value: 'owner', label: 'Owner' },
  { value: 'editor', label: 'Editor' },
  { value: 'viewer', label: 'Viewer' },
]

function ProjectsList() {
  const [selectedProjectId, setSelectedProjectId] = useState<string | null>(null)
  const [selectedProjectOwnerId, setSelectedProjectOwnerId] = useState<string | null>(null)
  const [projectModal, setProjectModal] = useState<{
    isOpen: boolean
    mode: 'create' | 'view' | 'edit'
    projectId?: string
  }>({ isOpen: false, mode: 'create' })

  const [searchInput, setSearchInput] = useState('')
  const [roleFilter, setRoleFilter] = useState('')
  const [debouncedSearch, setDebouncedSearch] = useState('')
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null)

  useEffect(() => {
    if (debounceRef.current) clearTimeout(debounceRef.current)
    debounceRef.current = setTimeout(() => {
      setDebouncedSearch(searchInput)
    }, 300)
    return () => {
      if (debounceRef.current) clearTimeout(debounceRef.current)
    }
  }, [searchInput])

  const queryParams = {
    ...(debouncedSearch ? { search: debouncedSearch } : {}),
    ...(roleFilter ? { role: roleFilter } : {}),
    limit: 20,
    offset: 0,
  }

  const { data, isLoading, error, isError } = useQuery({
    queryKey: ['projects', queryParams],
    queryFn: () => projectsApi.list(queryParams),
    retry: 1,
    refetchOnWindowFocus: false,
    staleTime: 0,
  })

  const { data: currentUser, isLoading: userLoading } = useQuery({
    queryKey: ['auth', 'me'],
    queryFn: () => authApi.me(),
  })

  const showContent = !isLoading && !isError && data

  const handleManageMembers = (projectId: string, ownerId: string) => {
    setSelectedProjectId(projectId)
    setSelectedProjectOwnerId(ownerId)
  }

  const handleCloseMembersModal = () => {
    setSelectedProjectId(null)
    setSelectedProjectOwnerId(null)
  }

  const openCreateProject = () => setProjectModal({ isOpen: true, mode: 'create' })

  const openProject = (projectId: string, ownerId: string) => {
    const mode = currentUser?.id === ownerId ? 'edit' : 'view'
    setProjectModal({ isOpen: true, mode, projectId })
  }

  const closeProjectModal = () => setProjectModal((prev) => ({ ...prev, isOpen: false }))

  const isProjectOwner = (projectOwnerId: string) => currentUser?.id === projectOwnerId
  const actionsDisabled = isLoading || userLoading

  return (
    <div className="projects-list">
      <div className="projects-filters" style={{ display: 'flex', gap: 12, marginBottom: 16, flexWrap: 'wrap' }}>
        <input
          type="text"
          value={searchInput}
          onChange={(e) => setSearchInput(e.target.value)}
          placeholder="Поиск проектов..."
          style={{ flex: '1 1 200px', minWidth: 160 }}
          aria-label="Поиск проектов"
        />
        <select
          value={roleFilter}
          onChange={(e) => setRoleFilter(e.target.value)}
          aria-label="Фильтр по роли"
        >
          {ROLE_OPTIONS.map((opt) => (
            <option key={opt.value} value={opt.value}>
              {opt.label}
            </option>
          ))}
        </select>
      </div>

      {isLoading && <Loading message="Загрузка проектов..." />}
      {isError && error && (
        <Error message={error instanceof Error ? error.message : 'Ошибка загрузки проектов'} />
      )}

      {showContent && data && (
        <>
          {data.projects.length === 0 ? (
            <EmptyState message="У вас пока нет проектов. Создайте первое рабочее пространство и свяжите с ним эксперименты, сенсоры и участников.">
              <button className="btn btn-primary" onClick={openCreateProject}>
                Создать первый проект
              </button>
            </EmptyState>
          ) : (
            <div className="projects-grid">
              {data.projects.map((project) => {
                const owner = isProjectOwner(project.owner_id)

                return (
                  <article key={project.id} className="project-card card">
                    <div className="project-card__eyebrow-row">
                      <span className={`meta-chip${owner ? ' meta-chip--owner' : ''}`}>
                        {owner ? 'Владелец' : 'Участник'}
                      </span>
                      <span className="project-card__date">
                        c {new Date(project.created_at).toLocaleDateString('ru-RU')}
                      </span>
                    </div>

                    <div className="project-card-header">
                      <div>
                        <h3>{project.name}</h3>
                        <p className="project-card__supporting-text">
                          {owner
                            ? 'Полный доступ к конфигурации проекта и составу команды.'
                            : 'Доступ к просмотру и совместной работе в рамках проекта.'}
                        </p>
                      </div>

                      <div className="project-card-actions">
                        <button
                          className="btn btn-secondary btn-sm"
                          onClick={() => openProject(project.id, project.owner_id)}
                          title={owner ? 'Просмотр и редактирование' : 'Просмотр'}
                          aria-label="Открыть проект"
                          disabled={actionsDisabled}
                        >
                          {owner ? 'Открыть' : 'Просмотр'}
                        </button>
                        {owner && (
                          <button
                            className="btn btn-secondary btn-sm"
                            onClick={() => handleManageMembers(project.id, project.owner_id)}
                            title="Управление участниками"
                            aria-label="Управление участниками"
                            disabled={actionsDisabled}
                          >
                            Команда
                          </button>
                        )}
                      </div>
                    </div>

                    <p className="project-description">
                      {project.description ||
                        'Добавьте описание проекта, чтобы команда быстрее ориентировалась в целях, ограничениях и контексте исследований.'}
                    </p>

                    <div className="project-card__meta-grid">
                      <div className="project-card__meta-item">
                        <span>Роль</span>
                        <strong>{owner ? 'Управление' : 'Совместная работа'}</strong>
                      </div>
                      <div className="project-card__meta-item">
                        <span>Дата создания</span>
                        <strong>{new Date(project.created_at).toLocaleDateString('ru-RU')}</strong>
                      </div>
                    </div>
                  </article>
                )
              })}
            </div>
          )}
        </>
      )}

      <ProjectModal
        isOpen={projectModal.isOpen}
        onClose={closeProjectModal}
        mode={projectModal.mode}
        projectId={projectModal.projectId}
      />

      {selectedProjectId && selectedProjectOwnerId && (
        <ProjectMembersModal
          isOpen={!!selectedProjectId}
          onClose={handleCloseMembersModal}
          projectId={selectedProjectId}
          projectOwnerId={selectedProjectOwnerId}
        />
      )}

      {typeof document !== 'undefined' &&
        createPortal(
          <FloatingActionButton
            onClick={openCreateProject}
            title="Создать проект"
            ariaLabel="Создать проект"
          />,
          document.body
        )}
    </div>
  )
}

export default ProjectsList
