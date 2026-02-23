-- Migration: conversion backfill tasks
-- Adds table for tracking background conversion profile backfill jobs.

CREATE TYPE backfill_task_status AS ENUM ('pending', 'running', 'completed', 'failed');

CREATE TABLE conversion_backfill_tasks (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    sensor_id uuid NOT NULL REFERENCES sensors(id) ON DELETE CASCADE,
    project_id uuid NOT NULL,
    conversion_profile_id uuid NOT NULL REFERENCES conversion_profiles(id),
    status backfill_task_status NOT NULL DEFAULT 'pending',
    total_records int,
    processed_records int NOT NULL DEFAULT 0,
    error_message text,
    created_by uuid NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    started_at timestamptz,
    completed_at timestamptz,
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX backfill_tasks_status_idx ON conversion_backfill_tasks (status);
CREATE INDEX backfill_tasks_sensor_idx ON conversion_backfill_tasks (sensor_id);

CREATE TRIGGER backfill_tasks_set_updated_at
    BEFORE UPDATE ON conversion_backfill_tasks
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at();
