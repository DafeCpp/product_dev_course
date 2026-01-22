import { useEffect, useMemo, useState } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { projectsApi } from '../api/client'
import { authApi } from '../api/auth'
import type { ProjectCreate, ProjectUpdate } from '../types'
import Modal from './Modal'
import { Error, InfoRow, Loading } from './common'
import { IS_TEST } from '../utils/env'
import { notifyError, notifySuccess } from '../utils/notify'
import './CreateRunModal.scss'

type ProjectModalMode = 'create' | 'view' | 'edit'
type ProjectUiMode = 'view' | 'edit'

interface ProjectModalProps {
    isOpen: boolean
    onClose: () => void
    mode: ProjectModalMode
    projectId?: string
}

function ProjectModal({ isOpen, onClose, mode, projectId }: ProjectModalProps) {
    const queryClient = useQueryClient()
    const [name, setName] = useState('')
    const [description, setDescription] = useState('')
    const [error, setError] = useState<string | null>(null)
    const [uiMode, setUiMode] = useState<ProjectUiMode>(mode === 'edit' ? 'edit' : 'view')

    const isCreate = mode === 'create'
    const needsProject = mode === 'view' || mode === 'edit'

    const { data: user } = useQuery({
        queryKey: ['auth', 'me'],
        queryFn: () => authApi.me(),
        enabled: isOpen && needsProject,
    })

    const {
        data: project,
        isLoading: projectLoading,
        isError: projectIsError,
        error: projectError,
    } = useQuery({
        queryKey: ['projects', projectId],
        queryFn: () => projectsApi.get(projectId!),
        enabled: isOpen && needsProject && !!projectId,
    })

    const {
        data: membersData,
        isLoading: membersLoading,
        isError: membersIsError,
        error: membersError,
    } = useQuery({
        queryKey: ['projects', projectId, 'members'],
        queryFn: () => projectsApi.listMembers(projectId!),
        enabled: isOpen && needsProject && !!projectId && !!user,
    })

    const role = useMemo<'owner' | 'editor' | 'viewer'>(() => {
        if (!needsProject) return 'owner'
        if (!user || !project) return 'viewer'
        if (user.id === project.owner_id) return 'owner'
        const member = membersData?.members?.find((m) => m.user_id === user.id)
        return member?.role ?? 'viewer'
    }, [membersData?.members, needsProject, project, user])

    const canEditByRole = role === 'owner' || role === 'editor'
    const canEdit = isCreate || (uiMode === 'edit' && canEditByRole)

    // Синхронизируем UI-режим с пропсом mode при открытии.
    // Важно: даже если нас открыли в режиме edit, но роль не позволяет — принудительно будем в view.
    useEffect(() => {
        if (!isOpen) return
        if (isCreate) {
            setUiMode('edit')
            return
        }
        setUiMode(mode === 'edit' ? 'edit' : 'view')
    }, [isOpen, isCreate, mode, projectId])

    useEffect(() => {
        if (!isOpen) return
        if (isCreate) return
        // Не "сбрасываем" edit-режим во время загрузки данных о проекте/участниках,
        // иначе modal, открытый в mode="edit", навсегда останется в view.
        const roleResolved =
            !!user &&
            !!project &&
            // если владелец — роли достаточно без списка участников
            (user.id === project.owner_id || (!membersLoading && (!!membersData || membersIsError)))

        if (uiMode === 'edit' && roleResolved && !canEditByRole) setUiMode('view')
    }, [canEditByRole, isCreate, isOpen, membersData, membersIsError, membersLoading, project, uiMode, user])

    useEffect(() => {
        if (!isOpen) return
        setError(null)

        if (isCreate) {
            setName('')
            setDescription('')
            return
        }

        if (project) {
            setName(project.name ?? '')
            setDescription(project.description ?? '')
        }
    }, [isOpen, isCreate, project])

    const roleLabel = (r: typeof role) => {
        switch (r) {
            case 'owner':
                return 'Владелец'
            case 'editor':
                return 'Редактор'
            case 'viewer':
                return 'Наблюдатель'
        }
    }

    const title = useMemo(() => {
        if (isCreate) return 'Создать проект'
        if (uiMode === 'edit' && canEditByRole) return 'Проект: просмотр и редактирование'
        return 'Проект: просмотр'
    }, [canEditByRole, isCreate, uiMode])

    const createMutation = useMutation({
        mutationFn: async (data: ProjectCreate) => projectsApi.create(data),
        onSuccess: async () => {
            await queryClient.invalidateQueries({ queryKey: ['projects'] })
            notifySuccess('Проект создан')
            onClose()
        },
        onError: (err: any) => {
            const msg =
                err?.response?.data?.error ||
                err?.response?.data?.message ||
                err?.message ||
                'Ошибка создания проекта'
            setError(msg)
            notifyError(msg)
        },
    })

    const updateMutation = useMutation({
        mutationFn: async (data: ProjectUpdate) => projectsApi.update(projectId!, data),
        onSuccess: async () => {
            await queryClient.invalidateQueries({ queryKey: ['projects'] })
            if (projectId) await queryClient.invalidateQueries({ queryKey: ['projects', projectId] })
            notifySuccess('Проект обновлён')
            onClose()
        },
        onError: (err: any) => {
            const msg =
                err?.response?.data?.error ||
                err?.response?.data?.message ||
                err?.message ||
                'Ошибка обновления проекта'
            setError(msg)
            notifyError(msg)
        },
    })

    const isBusy = createMutation.isPending || updateMutation.isPending
    const isLoading = (needsProject && (projectLoading || membersLoading)) || false
    const isErrorState = (needsProject && (projectIsError || membersIsError)) || false
    const combinedError = (projectError as any) || (membersError as any)

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        setError(null)

        if (!canEdit) return

        if (!name.trim()) {
            const msg = 'Название проекта обязательно'
            setError(msg)
            notifyError(msg)
            return
        }

        if (isCreate) {
            createMutation.mutate({
                name: name.trim(),
                description: description.trim() || undefined,
            })
            return
        }

        updateMutation.mutate({
            name: name.trim(),
            description: description.trim() || undefined,
        })
    }

    const startEditing = () => {
        if (isCreate) return
        if (!canEditByRole) return
        setError(null)
        setUiMode('edit')
    }

    const cancelEditing = () => {
        if (isCreate) return
        setError(null)
        setUiMode('view')
        // Возвращаем значения из проекта (если есть) — чтобы отмена реально отменяла правки.
        if (project) {
            setName(project.name ?? '')
            setDescription(project.description ?? '')
        }
    }

    const handleClose = () => {
        if (isBusy) return
        onClose()
    }

    return (
        <Modal
            isOpen={isOpen}
            onClose={handleClose}
            title={title}
            disabled={isBusy}
            className="project-modal"
        >
            {isLoading && <Loading />}

            {IS_TEST && isErrorState && (
                <Error
                    message={
                        combinedError && typeof combinedError === 'object' && 'message' in combinedError
                            ? String((combinedError as any).message)
                            : 'Ошибка загрузки проекта'
                    }
                />
            )}

            {!isLoading && !isErrorState && (
                <>
                    {IS_TEST && error && <div className="error">{error}</div>}

                    {!isCreate && project && (
                        <div className="user-info-section" style={{ marginBottom: '1rem' }}>
                            <h3>Информация</h3>
                            <div className="info-grid">
                                <InfoRow label="ID" value={<span className="mono">{project.id}</span>} />
                                <InfoRow label="Владелец" value={<span className="mono">{project.owner_id}</span>} />
                                <InfoRow label="Ваша роль" value={roleLabel(role)} />
                            </div>
                        </div>
                    )}

                    <form onSubmit={handleSubmit} className="modal-form">
                        <div className="form-group">
                            <label htmlFor="project_modal_name">
                                Название <span className="required">*</span>
                            </label>
                            <input
                                id="project_modal_name"
                                type="text"
                                value={name}
                                onChange={(e) => setName(e.target.value)}
                                required
                                disabled={!canEdit || isBusy}
                                placeholder="Например: Аэродинамические испытания"
                            />
                        </div>

                        <div className="form-group">
                            <label htmlFor="project_modal_description">Описание</label>
                            <textarea
                                id="project_modal_description"
                                value={description}
                                onChange={(e) => setDescription(e.target.value)}
                                rows={4}
                                disabled={!canEdit || isBusy}
                                placeholder="Описание проекта..."
                            />
                        </div>

                        {!canEdit && !isCreate && (
                            <small className="form-hint">
                                У вас роль <strong>{roleLabel(role)}</strong> — редактирование недоступно.
                            </small>
                        )}

                        <div className="modal-actions">
                            <button
                                type="button"
                                className="btn btn-secondary"
                                onClick={handleClose}
                                disabled={isBusy}
                            >
                                {isCreate ? 'Отмена' : 'Закрыть'}
                            </button>
                            {!isCreate && canEditByRole && uiMode === 'view' && (
                                <button
                                    type="button"
                                    className="btn btn-secondary"
                                    onClick={startEditing}
                                    disabled={isBusy}
                                >
                                    Редактировать
                                </button>
                            )}
                            {canEdit && (
                                <>
                                    {!isCreate && (
                                        <button
                                            type="button"
                                            className="btn btn-secondary"
                                            onClick={cancelEditing}
                                            disabled={isBusy}
                                        >
                                            Отмена
                                        </button>
                                    )}
                                    <button
                                        type="submit"
                                        className="btn btn-primary"
                                        disabled={isBusy || !name.trim()}
                                    >
                                        {isBusy ? 'Сохранение...' : isCreate ? 'Создать проект' : 'Сохранить'}
                                    </button>
                                </>
                            )}
                        </div>
                    </form>
                </>
            )}
        </Modal>
    )
}

export default ProjectModal

