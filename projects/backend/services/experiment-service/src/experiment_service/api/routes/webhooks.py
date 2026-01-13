"""Webhook subscription endpoints."""
from __future__ import annotations

from aiohttp import web
from pydantic import BaseModel, Field, ValidationError

from experiment_service.api.utils import paginated_response, pagination_params, parse_uuid, read_json
from experiment_service.core.exceptions import NotFoundError
from experiment_service.services.dependencies import (
    ensure_project_access,
    get_webhook_service,
    require_current_user,
    resolve_project_id,
)

routes = web.RouteTableDef()


class WebhookCreateDTO(BaseModel):
    target_url: str
    event_types: list[str] = Field(min_length=1)
    secret: str | None = None


@routes.get("/api/v1/webhooks")
async def list_webhooks(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_project_access(user, project_id)
    service = await get_webhook_service(request)
    limit, offset = pagination_params(request)
    items, total = await service.list_subscriptions(project_id, limit=limit, offset=offset)
    payload = paginated_response(
        [item.model_dump(mode="json") for item in items],
        limit=limit,
        offset=offset,
        key="webhooks",
        total=total,
    )
    return web.json_response(payload)


@routes.post("/api/v1/webhooks")
async def create_webhook(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    body = await read_json(request)
    try:
        dto = WebhookCreateDTO.model_validate(body)
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc

    # normalize event types
    event_types = [e.strip() for e in dto.event_types if e and e.strip()]
    event_types = list(dict.fromkeys(event_types))
    if not event_types:
        raise web.HTTPBadRequest(text="event_types must be a non-empty list")

    service = await get_webhook_service(request)
    sub = await service.create_subscription(
        project_id=project_id,
        target_url=dto.target_url,
        event_types=event_types,
        secret=dto.secret,
    )
    return web.json_response(sub.model_dump(mode="json"), status=201)


@routes.delete("/api/v1/webhooks/{webhook_id}")
async def delete_webhook(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    webhook_id = parse_uuid(request.match_info["webhook_id"], "webhook_id")
    service = await get_webhook_service(request)
    try:
        await service.delete_subscription(project_id, webhook_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.Response(status=204)


@routes.get("/api/v1/webhooks/deliveries")
async def list_webhook_deliveries(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_project_access(user, project_id)
    status = request.rel_url.query.get("status")
    limit, offset = pagination_params(request)
    service = await get_webhook_service(request)
    items, total = await service.list_deliveries(project_id, status=status, limit=limit, offset=offset)
    payload = paginated_response(
        [item.model_dump(mode="json") for item in items],
        limit=limit,
        offset=offset,
        key="deliveries",
        total=total,
    )
    return web.json_response(payload)


@routes.post("/api/v1/webhooks/deliveries/{delivery_id}:retry")
async def retry_webhook_delivery(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    delivery_id = parse_uuid(request.match_info["delivery_id"], "delivery_id")
    service = await get_webhook_service(request)
    try:
        await service.retry_delivery(project_id, delivery_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.Response(status=204)

