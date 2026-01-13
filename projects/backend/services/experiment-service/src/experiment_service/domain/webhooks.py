"""Webhook domain primitives."""
from __future__ import annotations

from datetime import datetime
from typing import Any
from uuid import UUID

from pydantic import BaseModel, Field, HttpUrl


class WebhookSubscription(BaseModel):
    id: UUID
    project_id: UUID
    target_url: str
    secret: str | None = None
    event_types: list[str] = Field(default_factory=list)
    is_active: bool = True
    created_at: datetime
    updated_at: datetime


class WebhookDelivery(BaseModel):
    id: UUID
    subscription_id: UUID
    project_id: UUID
    event_type: str
    target_url: str
    secret: str | None = None
    request_body: dict[str, Any]
    status: str
    attempt_count: int
    last_error: str | None = None
    dedup_key: str | None = None
    locked_at: datetime | None = None
    next_attempt_at: datetime
    created_at: datetime
    updated_at: datetime

