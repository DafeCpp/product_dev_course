const STORAGE_KEY = 'experiment_portal.active_project_id'
const WORKSPACE_STORAGE_KEYS = [
    STORAGE_KEY,
    'telemetry_panel_ids',
    'telemetry_history_state',
    'telemetry_viewer_state',
]
const TELEMETRY_PANEL_STORAGE_PREFIX = 'telemetry_panel_state_'

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

export function clearWorkspaceStorage() {
    try {
        WORKSPACE_STORAGE_KEYS.forEach((key) => window.localStorage.removeItem(key))

        for (let index = window.localStorage.length - 1; index >= 0; index -= 1) {
            const key = window.localStorage.key(index)
            if (key?.startsWith(TELEMETRY_PANEL_STORAGE_PREFIX)) {
                window.localStorage.removeItem(key)
            }
        }
    } catch {
        // ignore (e.g. storage disabled)
    }
}
