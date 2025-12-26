// Status mappings for different entity types

export const experimentStatusMap = {
    draft: { badgeClass: 'badge-secondary', text: 'Черновик' },
    running: { badgeClass: 'badge-info', text: 'Выполняется' },
    succeeded: { badgeClass: 'badge-success', text: 'Успешно' },
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
    draft: { badgeClass: 'badge-secondary', text: 'Черновик' },
    running: { badgeClass: 'badge-info', text: 'Выполняется' },
    succeeded: { badgeClass: 'badge-success', text: 'Успешно' },
    failed: { badgeClass: 'badge-danger', text: 'Ошибка' },
    archived: { badgeClass: 'badge-secondary', text: 'Архивирован' },
}

export const captureSessionStatusMap = {
    draft: { badgeClass: 'badge-secondary', text: 'Черновик' },
    running: { badgeClass: 'badge-info', text: 'Выполняется' },
    succeeded: { badgeClass: 'badge-success', text: 'Успешно' },
    failed: { badgeClass: 'badge-danger', text: 'Ошибка' },
    archived: { badgeClass: 'badge-secondary', text: 'Архивирован' },
    backfilling: { badgeClass: 'badge-warning', text: 'Дозаполнение' },
}

