import { useState } from 'react'
import { useApiMutation } from '../../hooks/useApiMutation'
import { permissionsApi } from '../../api/permissions'
import Modal from '../Modal'
import { notifyError } from '../../utils/notify'
import PermissionPickerLocal from './PermissionPickerLocal'

interface CreateProjectRoleModalProps {
    isOpen: boolean
    onClose: () => void
    projectId: string
}

function CreateProjectRoleModal({ isOpen, onClose, projectId }: CreateProjectRoleModalProps) {
    const [name, setName] = useState('')
    const [description, setDescription] = useState('')
    const [permissionIds, setPermissionIds] = useState<string[]>([])

    const createMutation = useApiMutation({
        mutationFn: () =>
            permissionsApi.createProjectRole(projectId, {
                name,
                description: description || undefined,
                permissions: permissionIds,
            }),
        invalidateKeys: [['projects', projectId, 'roles']],
        successMessage: 'Роль создана',
        errorFallback: 'Ошибка создания роли',
        onSuccess: () => { setName(''); setDescription(''); setPermissionIds([]); onClose() },
    })

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        if (!name.trim()) {
            notifyError('Введите название роли')
            return
        }
        createMutation.mutate()
    }

    const handleClose = () => {
        if (!createMutation.isPending) {
            setName('')
            setDescription('')
            setPermissionIds([])
            onClose()
        }
    }

    return (
        <Modal
            isOpen={isOpen}
            onClose={handleClose}
            title="Создать роль проекта"
            disabled={createMutation.isPending}
            className="create-project-role-modal"
        >
            <div className="modal-form">
                <form onSubmit={handleSubmit}>
                    <div className="form-group">
                        <label htmlFor="role_name">
                            Название <span className="required">*</span>
                        </label>
                        <input
                            id="role_name"
                            type="text"
                            className="form-control"
                            value={name}
                            onChange={(e) => setName(e.target.value)}
                            disabled={createMutation.isPending}
                            placeholder="Например: reviewer"
                            required
                        />
                    </div>
                    <div className="form-group">
                        <label htmlFor="role_description">Описание</label>
                        <input
                            id="role_description"
                            type="text"
                            className="form-control"
                            value={description}
                            onChange={(e) => setDescription(e.target.value)}
                            disabled={createMutation.isPending}
                            placeholder="Необязательно"
                        />
                    </div>
                    <div className="form-group">
                        <label>Разрешения</label>
                        <PermissionPickerLocal
                            scope="project"
                            value={permissionIds}
                            onChange={setPermissionIds}
                            disabled={createMutation.isPending}
                        />
                    </div>
                    <div className="modal-actions">
                        <button
                            type="submit"
                            className="btn btn-primary"
                            disabled={createMutation.isPending || !name.trim()}
                        >
                            {createMutation.isPending ? 'Создание...' : 'Создать роль'}
                        </button>
                        <button
                            type="button"
                            className="btn btn-secondary"
                            onClick={handleClose}
                            disabled={createMutation.isPending}
                        >
                            Отмена
                        </button>
                    </div>
                </form>
            </div>
        </Modal>
    )
}

export default CreateProjectRoleModal
