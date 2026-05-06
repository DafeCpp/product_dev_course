import { describe, it, expect } from 'vitest'
import {
    createMockUser,
    createMockAdminUser,
    createMockProject,
    createMockProjectMember,
    createMockExperiment,
    createMockRun,
    createMockSensor,
    createMockCaptureSession,
} from './factories'

describe('createMockUser', () => {
    it('returns a complete User object with required fields', () => {
        const user = createMockUser()

        expect(user).toMatchObject({
            id: expect.any(String),
            username: expect.any(String),
            email: expect.any(String),
            is_active: expect.any(Boolean),
            created_at: expect.any(String),
        })
    })

    it('applies overrides correctly', () => {
        const user = createMockUser({ id: 'custom-id', is_admin: true, system_roles: ['admin'] })

        expect(user.id).toBe('custom-id')
        expect(user.is_admin).toBe(true)
        expect(user.system_roles).toEqual(['admin'])
    })

    it('preserves defaults for fields not in overrides', () => {
        const user = createMockUser({ id: 'only-id-changed' })

        expect(user.username).toBe('testuser')
        expect(user.email).toBe('test@example.com')
        expect(user.is_active).toBe(true)
    })
})

describe('createMockAdminUser', () => {
    it('returns a complete AdminUser object with required fields', () => {
        const user = createMockAdminUser()

        expect(user).toMatchObject({
            id: expect.any(String),
            username: expect.any(String),
            email: expect.any(String),
            is_active: expect.any(Boolean),
            is_admin: expect.any(Boolean),
            system_roles: expect.any(Array),
            password_change_required: expect.any(Boolean),
            created_at: expect.any(String),
        })
    })

    it('applies overrides correctly', () => {
        const user = createMockAdminUser({ is_admin: true, password_change_required: true })

        expect(user.is_admin).toBe(true)
        expect(user.password_change_required).toBe(true)
    })
})

describe('createMockProject', () => {
    it('returns a complete Project object with required fields', () => {
        const project = createMockProject()

        expect(project).toMatchObject({
            id: expect.any(String),
            name: expect.any(String),
            owner_id: expect.any(String),
            created_at: expect.any(String),
            updated_at: expect.any(String),
        })
    })

    it('applies overrides correctly', () => {
        const project = createMockProject({ id: 'proj-99', name: 'Custom Project', description: null })

        expect(project.id).toBe('proj-99')
        expect(project.name).toBe('Custom Project')
        expect(project.description).toBeNull()
    })

    it('preserves defaults for fields not in overrides', () => {
        const project = createMockProject({ name: 'Only Name Changed' })

        expect(project.id).toBe('project-1')
        expect(project.owner_id).toBe('user-1')
    })
})

describe('createMockProjectMember', () => {
    it('returns a complete ProjectMember object with required fields', () => {
        const member = createMockProjectMember()

        expect(member).toMatchObject({
            project_id: expect.any(String),
            user_id: expect.any(String),
            role: expect.stringMatching(/^(owner|editor|viewer)$/),
            created_at: expect.any(String),
        })
    })

    it('applies overrides correctly', () => {
        const member = createMockProjectMember({ role: 'editor', user_id: 'user-42' })

        expect(member.role).toBe('editor')
        expect(member.user_id).toBe('user-42')
    })
})

describe('createMockExperiment', () => {
    it('returns a complete Experiment object with required fields', () => {
        const experiment = createMockExperiment()

        expect(experiment).toMatchObject({
            id: expect.any(String),
            project_id: expect.any(String),
            name: expect.any(String),
            owner_id: expect.any(String),
            status: expect.any(String),
            tags: expect.any(Array),
            metadata: expect.any(Object),
            created_at: expect.any(String),
            updated_at: expect.any(String),
        })
    })

    it('has a valid default status value', () => {
        const experiment = createMockExperiment()
        const validStatuses = ['draft', 'running', 'succeeded', 'failed', 'archived']

        expect(validStatuses).toContain(experiment.status)
    })

    it('applies overrides correctly', () => {
        const experiment = createMockExperiment({
            id: 'exp-999',
            status: 'running',
            tags: ['alpha', 'beta'],
            metadata: {},
        })

        expect(experiment.id).toBe('exp-999')
        expect(experiment.status).toBe('running')
        expect(experiment.tags).toEqual(['alpha', 'beta'])
        expect(experiment.metadata).toEqual({})
    })

    it('preserves defaults for fields not in overrides', () => {
        const experiment = createMockExperiment({ name: 'Custom Name' })

        expect(experiment.project_id).toBe('project-1')
        expect(experiment.owner_id).toBe('user-1')
        expect(experiment.status).toBe('draft')
    })
})

describe('createMockRun', () => {
    it('returns a complete Run object with required fields', () => {
        const run = createMockRun()

        expect(run).toMatchObject({
            id: expect.any(String),
            experiment_id: expect.any(String),
            name: expect.any(String),
            params: expect.any(Object),
            status: expect.any(String),
            metadata: expect.any(Object),
            auto_complete_after_minutes: null,
            created_at: expect.any(String),
            updated_at: expect.any(String),
        })
    })

    it('has a valid default status value', () => {
        const run = createMockRun()
        const validStatuses = ['draft', 'running', 'succeeded', 'failed', 'archived']

        expect(validStatuses).toContain(run.status)
    })

    it('applies overrides correctly', () => {
        const run = createMockRun({
            id: 'run-999',
            status: 'succeeded',
            finished_at: '2024-01-02T10:00:00Z',
            auto_complete_after_minutes: 30,
        })

        expect(run.id).toBe('run-999')
        expect(run.status).toBe('succeeded')
        expect(run.finished_at).toBe('2024-01-02T10:00:00Z')
        expect(run.auto_complete_after_minutes).toBe(30)
    })

    it('preserves defaults for fields not in overrides', () => {
        const run = createMockRun({ name: 'Custom Run' })

        expect(run.experiment_id).toBe('exp-123')
        expect(run.status).toBe('running')
        expect(run.duration_seconds).toBe(3600)
    })
})

describe('createMockSensor', () => {
    it('returns a complete Sensor object with required fields', () => {
        const sensor = createMockSensor()

        expect(sensor).toMatchObject({
            id: expect.any(String),
            project_id: expect.any(String),
            name: expect.any(String),
            type: expect.any(String),
            input_unit: expect.any(String),
            display_unit: expect.any(String),
            status: expect.any(String),
            created_at: expect.any(String),
            updated_at: expect.any(String),
        })
    })

    it('has a valid default status value', () => {
        const sensor = createMockSensor()
        const validStatuses = ['registering', 'active', 'inactive', 'archived']

        expect(validStatuses).toContain(sensor.status)
    })

    it('applies overrides correctly', () => {
        const sensor = createMockSensor({
            id: 'sensor-99',
            status: 'inactive',
            calibration_notes: 'Needs recalibration',
            connection_status: 'offline',
        })

        expect(sensor.id).toBe('sensor-99')
        expect(sensor.status).toBe('inactive')
        expect(sensor.calibration_notes).toBe('Needs recalibration')
        expect(sensor.connection_status).toBe('offline')
    })

    it('preserves defaults for fields not in overrides', () => {
        const sensor = createMockSensor({ name: 'Pressure Sensor' })

        expect(sensor.project_id).toBe('project-1')
        expect(sensor.type).toBe('temperature')
        expect(sensor.status).toBe('active')
    })
})

describe('createMockCaptureSession', () => {
    it('returns a complete CaptureSession object with required fields', () => {
        const session = createMockCaptureSession()

        expect(session).toMatchObject({
            id: expect.any(String),
            run_id: expect.any(String),
            project_id: expect.any(String),
            ordinal_number: expect.any(Number),
            status: expect.any(String),
            archived: expect.any(Boolean),
            created_at: expect.any(String),
            updated_at: expect.any(String),
        })
    })

    it('has a valid default status value', () => {
        const session = createMockCaptureSession()
        const validStatuses = ['draft', 'running', 'failed', 'succeeded', 'archived', 'backfilling']

        expect(validStatuses).toContain(session.status)
    })

    it('applies overrides correctly', () => {
        const session = createMockCaptureSession({
            id: 'session-456',
            ordinal_number: 2,
            status: 'succeeded',
            stopped_at: '2024-01-01T11:00:00Z',
            archived: true,
        })

        expect(session.id).toBe('session-456')
        expect(session.ordinal_number).toBe(2)
        expect(session.status).toBe('succeeded')
        expect(session.stopped_at).toBe('2024-01-01T11:00:00Z')
        expect(session.archived).toBe(true)
    })

    it('preserves defaults for fields not in overrides', () => {
        const session = createMockCaptureSession({ id: 'only-id-changed' })

        expect(session.run_id).toBe('run-123')
        expect(session.project_id).toBe('project-1')
        expect(session.ordinal_number).toBe(1)
        expect(session.archived).toBe(false)
    })
})
