import { useState, useMemo } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { permissionsApi } from '../api/permissions'
import type { Role } from '../types/permissions'
import { Loading, Error as ErrorComponent, EmptyState } from '../components/common'
import PermissionGate from '../components/PermissionGate'
import PermissionPicker from '../components/PermissionPicker'
import { usePermissions } from '../hooks/usePermissions'
import { notifySuccess, notifyError } from '../utils/notify'
import './SystemRoles.scss'

interface RoleFormState {
  name: string
  description: string
  selectedPermissions: string[]
}

const EMPTY_FORM: RoleFormState = {
  name: '',
  description: '',
  selectedPermissions: [],
}

function SystemRoles() {
  const queryClient = useQueryClient()
  const { hasSystemPermission } = usePermissions()

  const [search, setSearch] = useState('')
  const [modalOpen, setModalOpen] = useState(false)
  const [editingRole, setEditingRole] = useState<Role | null>(null)
  const [form, setForm] = useState<RoleFormState>(EMPTY_FORM)

  const {
    data: roles = [],
    isLoading,
    error,
  } = useQuery({
    queryKey: ['system-roles'],
    queryFn: () => permissionsApi.listSystemRoles(),
  })

  const filteredRoles = useMemo(() => {
    const q = search.trim().toLowerCase()
    if (!q) return roles
    return roles.filter(
      (r) =>
        r.name.toLowerCase().includes(q) ||
        (r.description?.toLowerCase().includes(q) ?? false)
    )
  }, [roles, search])

  const createMutation = useMutation({
    mutationFn: (data: { name: string; description?: string; permissions: string[] }) =>
      permissionsApi.createSystemRole(data),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['system-roles'] })
      notifySuccess('Роль создана')
      closeModal()
    },
    onError: (err: unknown) => {
      const msg = extractErrorMessage(err, 'Не удалось создать роль')
      notifyError(msg)
    },
  })

  const updateMutation = useMutation({
    mutationFn: ({
      id,
      data,
    }: {
      id: string
      data: { name?: string; description?: string; permissions?: string[] }
    }) => permissionsApi.updateSystemRole(id, data),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['system-roles'] })
      notifySuccess('Роль обновлена')
      closeModal()
    },
    onError: (err: unknown) => {
      const msg = extractErrorMessage(err, 'Не удалось обновить роль')
      notifyError(msg)
    },
  })

  const deleteMutation = useMutation({
    mutationFn: (id: string) => permissionsApi.deleteSystemRole(id),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['system-roles'] })
      notifySuccess('Роль удалена')
    },
    onError: (err: unknown) => {
      const msg = extractErrorMessage(err, 'Не удалось удалить роль')
      notifyError(msg)
    },
  })

  function openCreateModal() {
    setEditingRole(null)
    setForm(EMPTY_FORM)
    setModalOpen(true)
  }

  function openEditModal(role: Role) {
    setEditingRole(role)
    setForm({
      name: role.name,
      description: role.description ?? '',
      selectedPermissions: role.permissions.map((p) => p.name),
    })
    setModalOpen(true)
  }

  function closeModal() {
    setModalOpen(false)
    setEditingRole(null)
    setForm(EMPTY_FORM)
  }

  function handleDelete(role: Role) {
    if (!confirm(`Удалить роль «${role.name}»?`)) return
    deleteMutation.mutate(role.id)
  }

  function handleSubmit() {
    const name = form.name.trim()
    if (!name) {
      notifyError('Название роли обязательно')
      return
    }

    const payload = {
      name,
      description: form.description.trim() || undefined,
      permissions: form.selectedPermissions,
    }

    if (editingRole) {
      updateMutation.mutate({ id: editingRole.id, data: payload })
    } else {
      createMutation.mutate(payload)
    }
  }

  const isSaving = createMutation.isPending || updateMutation.isPending

  if (!hasSystemPermission('roles.manage')) {
    return (
      <div className="system-roles-page">
        <div className="access-denied card">
          <div className="access-denied__icon">&#128274;</div>
          <h3>Нет доступа</h3>
          <p>У вас нет прав для управления системными ролями.</p>
        </div>
      </div>
    )
  }

  return (
    <div className="system-roles-page">
      <div className="card">
        <div className="card-header">
          <h3>Системные роли</h3>
          <div className="header-controls">
            <input
              type="search"
              className="search-input"
              placeholder="Поиск по названию..."
              value={search}
              onChange={(e) => setSearch(e.target.value)}
            />
            <PermissionGate permission="roles.manage" system>
              <button className="btn btn-primary btn-sm" onClick={openCreateModal}>
                Создать роль
              </button>
            </PermissionGate>
          </div>
        </div>

        {isLoading && <Loading message="Загрузка ролей..." />}
        {error && (
          <ErrorComponent
            message={
              error instanceof Error ? error.message : 'Ошибка загрузки ролей'
            }
          />
        )}

        {!isLoading && !error && filteredRoles.length === 0 && (
          <EmptyState message={search ? 'Ничего не найдено' : 'Ролей пока нет'} />
        )}

        {!isLoading && !error && filteredRoles.length > 0 && (
          <div className="roles-table-wrap">
            <table className="roles-table">
              <thead>
                <tr>
                  <th>Название</th>
                  <th>Описание</th>
                  <th>Тип</th>
                  <th>Permissions</th>
                  <th>Действия</th>
                </tr>
              </thead>
              <tbody>
                {filteredRoles.map((role) => (
                  <tr key={role.id}>
                    <td className="role-name-cell">
                      <span className="role-name">{role.name}</span>
                    </td>
                    <td className="role-desc-cell">
                      {role.description ?? <span className="text-muted">—</span>}
                    </td>
                    <td>
                      {role.is_builtin ? (
                        <span className="role-type-badge role-type-badge--builtin">
                          Встроенная
                        </span>
                      ) : (
                        <span className="role-type-badge role-type-badge--custom">
                          Кастомная
                        </span>
                      )}
                    </td>
                    <td className="role-perms-cell">
                      {role.permissions.length === 0 ? (
                        <span className="text-muted">—</span>
                      ) : (
                        <div className="perm-tags">
                          {role.permissions.map((p) => (
                            <span key={p.id} className="perm-tag">
                              {p.name}
                            </span>
                          ))}
                        </div>
                      )}
                    </td>
                    <td className="actions-cell">
                      {!role.is_builtin && (
                        <>
                          <button
                            className="btn btn-secondary btn-sm"
                            onClick={() => openEditModal(role)}
                          >
                            Редактировать
                          </button>
                          <button
                            className="btn btn-danger btn-sm"
                            onClick={() => handleDelete(role)}
                            disabled={deleteMutation.isPending}
                          >
                            Удалить
                          </button>
                        </>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      {modalOpen && (
        <div className="modal-overlay" onClick={closeModal}>
          <div className="modal" onClick={(e) => e.stopPropagation()}>
            <div className="modal-header">
              <h3>{editingRole ? 'Редактировать роль' : 'Создать роль'}</h3>
              <button className="modal-close" onClick={closeModal} aria-label="Закрыть">
                &#10005;
              </button>
            </div>
            <div className="modal-body">
              <div className="form-group">
                <label htmlFor="role-name">Название *</label>
                <input
                  id="role-name"
                  type="text"
                  placeholder="Название роли"
                  value={form.name}
                  onChange={(e) => setForm((f) => ({ ...f, name: e.target.value }))}
                  disabled={isSaving}
                />
              </div>
              <div className="form-group">
                <label htmlFor="role-desc">Описание</label>
                <input
                  id="role-desc"
                  type="text"
                  placeholder="Описание роли (опционально)"
                  value={form.description}
                  onChange={(e) => setForm((f) => ({ ...f, description: e.target.value }))}
                  disabled={isSaving}
                />
              </div>
              <div className="form-group">
                <label>Permissions</label>
                <PermissionPicker
                  scope="system"
                  selected={form.selectedPermissions}
                  onChange={(selected) => setForm((f) => ({ ...f, selectedPermissions: selected }))}
                  disabled={isSaving}
                />
              </div>
            </div>
            <div className="modal-footer">
              <button
                className="btn btn-secondary btn-sm"
                onClick={closeModal}
                disabled={isSaving}
              >
                Отмена
              </button>
              <button
                className="btn btn-primary btn-sm"
                onClick={handleSubmit}
                disabled={isSaving}
              >
                {isSaving ? 'Сохранение...' : 'Сохранить'}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

function extractErrorMessage(err: unknown, fallback: string): string {
  if (err && typeof err === 'object') {
    const e = err as Record<string, unknown>
    const resp = e['response']
    if (resp && typeof resp === 'object') {
      const data = (resp as Record<string, unknown>)['data']
      if (data && typeof data === 'object') {
        const d = data as Record<string, unknown>
        if (typeof d['message'] === 'string') return d['message']
        if (typeof d['error'] === 'string') return d['error']
      }
    }
    if (typeof e['message'] === 'string') return e['message']
  }
  return fallback
}

export default SystemRoles
