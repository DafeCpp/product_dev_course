import { beforeEach, describe, expect, it } from 'vitest'
import { clearWorkspaceStorage } from './activeProject'

describe('clearWorkspaceStorage', () => {
    beforeEach(() => {
        window.localStorage.clear()
    })

    it('removes persisted workspace state while preserving unrelated preferences', () => {
        window.localStorage.setItem('experiment_portal.active_project_id', 'project-1')
        window.localStorage.setItem('telemetry_panel_ids', '["panel-1"]')
        window.localStorage.setItem('telemetry_history_state', '{"sensorIds":["sensor-1"]}')
        window.localStorage.setItem('telemetry_viewer_state', '{"projectId":"project-1"}')
        window.localStorage.setItem('telemetry_panel_state_panel-1', '{"selectedSensorIds":["sensor-1"]}')
        window.localStorage.setItem('telemetry_panel_state_panel-2', '{"selectedSensorIds":["sensor-2"]}')
        window.localStorage.setItem('experiment_portal_sidebar_desktop_collapsed', '1')
        window.localStorage.setItem('unrelated_key', 'keep')

        clearWorkspaceStorage()

        expect(window.localStorage.getItem('experiment_portal.active_project_id')).toBeNull()
        expect(window.localStorage.getItem('telemetry_panel_ids')).toBeNull()
        expect(window.localStorage.getItem('telemetry_history_state')).toBeNull()
        expect(window.localStorage.getItem('telemetry_viewer_state')).toBeNull()
        expect(window.localStorage.getItem('telemetry_panel_state_panel-1')).toBeNull()
        expect(window.localStorage.getItem('telemetry_panel_state_panel-2')).toBeNull()
        expect(window.localStorage.getItem('experiment_portal_sidebar_desktop_collapsed')).toBe('1')
        expect(window.localStorage.getItem('unrelated_key')).toBe('keep')
    })
})
