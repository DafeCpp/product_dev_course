-- Attaching a sensor to another project goes through a request that a member of a
-- project the sensor already belongs to approves. Without it anyone who knew a
-- sensor UUID could attach it to their own project and read its telemetry.

CREATE TABLE IF NOT EXISTS sensor_project_requests (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    sensor_id uuid NOT NULL REFERENCES sensors (id) ON DELETE CASCADE,
    project_id uuid NOT NULL,
    requested_by uuid NOT NULL,
    status text NOT NULL DEFAULT 'pending'
        CHECK (status IN ('pending', 'approved', 'rejected', 'cancelled')),
    decided_by uuid,
    decided_in_project_id uuid,
    created_at timestamptz NOT NULL DEFAULT now(),
    decided_at timestamptz
);

-- At most one open request per sensor and target project.
CREATE UNIQUE INDEX IF NOT EXISTS sensor_project_requests_pending_uniq
    ON sensor_project_requests (sensor_id, project_id)
    WHERE status = 'pending';
CREATE INDEX IF NOT EXISTS sensor_project_requests_project_idx
    ON sensor_project_requests (project_id, status);
CREATE INDEX IF NOT EXISTS sensor_project_requests_sensor_idx
    ON sensor_project_requests (sensor_id, status);

COMMENT ON TABLE sensor_project_requests IS
    'Requests to attach a sensor to a project; approved by a member of a project the sensor already belongs to.';
