/**
 * Factory functions for test mock objects.
 *
 * Each factory returns a complete, valid object with sensible defaults.
 * Pass `overrides` to customize specific fields:
 *
 *   const admin = createMockUser({ is_admin: true, system_roles: ['admin'] })
 *   const exp = createMockExperiment({ status: 'running', project_id: 'proj-42' })
 */

import type {
    User,
    Project,
    Experiment,
    Run,
    Sensor,
    ProjectMember,
    CaptureSession,
    AdminUser,
} from '../types'

// ---------------------------------------------------------------------------
// User
// ---------------------------------------------------------------------------

export function createMockUser(overrides?: Partial<User>): User {
    return {
        id: 'user-1',
        username: 'testuser',
        email: 'test@example.com',
        is_active: true,
        is_admin: false,
        system_roles: [],
        effective_permissions: [],
        password_change_required: false,
        created_at: '2024-01-01T00:00:00Z',
        ...overrides,
    }
}

// ---------------------------------------------------------------------------
// AdminUser
// ---------------------------------------------------------------------------

export function createMockAdminUser(overrides?: Partial<AdminUser>): AdminUser {
    return {
        id: 'user-1',
        username: 'testuser',
        email: 'test@example.com',
        is_active: true,
        is_admin: false,
        system_roles: [],
        password_change_required: false,
        created_at: '2024-01-01T00:00:00Z',
        ...overrides,
    }
}

// ---------------------------------------------------------------------------
// Project
// ---------------------------------------------------------------------------

export function createMockProject(overrides?: Partial<Project>): Project {
    return {
        id: 'project-1',
        name: 'Test Project',
        description: 'A test project description',
        owner_id: 'user-1',
        created_at: '2024-01-01T00:00:00Z',
        updated_at: '2024-01-01T00:00:00Z',
        ...overrides,
    }
}

// ---------------------------------------------------------------------------
// ProjectMember
// ---------------------------------------------------------------------------

export function createMockProjectMember(overrides?: Partial<ProjectMember>): ProjectMember {
    return {
        project_id: 'project-1',
        user_id: 'user-1',
        role: 'owner',
        roles: [],
        effective_permissions: [],
        created_at: '2024-01-01T00:00:00Z',
        username: 'testuser',
        ...overrides,
    }
}

// ---------------------------------------------------------------------------
// Experiment
// ---------------------------------------------------------------------------

export function createMockExperiment(overrides?: Partial<Experiment>): Experiment {
    return {
        id: 'exp-123',
        project_id: 'project-1',
        name: 'Test Experiment',
        description: 'Test description',
        experiment_type: 'benchmark',
        owner_id: 'user-1',
        status: 'draft',
        tags: ['test', 'benchmark'],
        metadata: { key: 'value' },
        created_at: '2024-01-01T00:00:00Z',
        updated_at: '2024-01-02T00:00:00Z',
        ...overrides,
    }
}

// ---------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------

export function createMockRun(overrides?: Partial<Run>): Run {
    return {
        id: 'run-123',
        experiment_id: 'exp-123',
        name: 'Test Run',
        params: { param1: 'value1', param2: 'value2' },
        status: 'running',
        tags: [],
        started_at: '2024-01-01T10:00:00Z',
        finished_at: undefined,
        duration_seconds: 3600,
        notes: 'Test notes',
        metadata: { key: 'value' },
        auto_complete_after_minutes: null,
        created_at: '2024-01-01T00:00:00Z',
        updated_at: '2024-01-01T12:00:00Z',
        ...overrides,
    }
}

// ---------------------------------------------------------------------------
// Sensor
// ---------------------------------------------------------------------------

export function createMockSensor(overrides?: Partial<Sensor>): Sensor {
    return {
        id: 'sensor-1',
        project_id: 'project-1',
        name: 'Temperature Sensor #1',
        type: 'temperature',
        input_unit: 'V',
        display_unit: '°C',
        status: 'active',
        connection_status: 'online',
        token_preview: 'abcd',
        last_heartbeat: '2024-01-01T12:00:00Z',
        active_profile_id: null,
        calibration_notes: null,
        created_at: '2024-01-01T00:00:00Z',
        updated_at: '2024-01-01T00:00:00Z',
        ...overrides,
    }
}

// ---------------------------------------------------------------------------
// CaptureSession
// ---------------------------------------------------------------------------

export function createMockCaptureSession(overrides?: Partial<CaptureSession>): CaptureSession {
    return {
        id: 'session-123',
        run_id: 'run-123',
        project_id: 'project-1',
        ordinal_number: 1,
        started_at: '2024-01-01T10:00:00Z',
        stopped_at: null,
        status: 'running',
        initiated_by: 'user-1',
        notes: 'Test session',
        archived: false,
        created_at: '2024-01-01T10:00:00Z',
        updated_at: '2024-01-01T10:00:00Z',
        ...overrides,
    }
}
