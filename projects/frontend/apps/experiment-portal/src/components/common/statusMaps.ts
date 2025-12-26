// Status mappings for different entity types

export const experimentStatusMap = {
    created: { badgeClass: 'badge-secondary', text: 'Создан' },
    running: { badgeClass: 'badge-info', text: 'Выполняется' },
    completed: { badgeClass: 'badge-success', text: 'Завершен' },
    failed: { badgeClass: 'badge-danger', text: 'Ошибка' },
    archived: { badgeClass: 'badge-secondary', text: 'Архивирован' },
}

export const sensorStatusMap = {
    registering: { badgeClass: 'badge-secondary', text: 'Регистрация' },
    active: { badgeClass: 'badge-success', text: 'Активен' },
    inactive: { badgeClass: 'badge-warning', text: 'Неактивен' },
    archived: { badgeClass: 'badge-secondary', text: 'Архивирован' },
}

export const runStatusMap = {
    created: { badgeClass: 'badge-secondary', text: 'Создан' },
    running: { badgeClass: 'badge-info', text: 'Выполняется' },
    completed: { badgeClass: 'badge-success', text: 'Завершен' },
    failed: { badgeClass: 'badge-danger', text: 'Ошибка' },
}

export const captureSessionStatusMap = {
    draft: { badgeClass: 'badge-secondary', text: 'Черновик' },
    running: { badgeClass: 'badge-info', text: 'Выполняется' },
    succeeded: { badgeClass: 'badge-success', text: 'Успешно' },
    failed: { badgeClass: 'badge-danger', text: 'Ошибка' },
    archived: { badgeClass: 'badge-secondary', text: 'Архивирован' },
    backfilling: { badgeClass: 'badge-warning', text: 'Дозаполнение' },
}

