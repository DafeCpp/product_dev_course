const STORAGE_KEY = 'experiment_portal.active_project_id'

export function getActiveProjectId(): string | null {
    try {
        return window.localStorage.getItem(STORAGE_KEY)
    } catch {
        return null
    }
}

export function setActiveProjectId(projectId: string) {
    try {
        if (projectId) {
            window.localStorage.setItem(STORAGE_KEY, projectId)
        } else {
            window.localStorage.removeItem(STORAGE_KEY)
        }
    } catch {
        // ignore (e.g. storage disabled)
    }
}

