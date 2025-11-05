"""Обработчики для runs."""
import logging
from uuid import UUID
from aiohttp import web
from aiohttp.web_request import Request

from src.schemas import (
    RunCreate,
    RunUpdate,
    RunResponse,
    RunListResponse
)
from src.queries import runs as run_queries
from src.queries import experiments as experiment_queries
from src.events import publish_event

logger = logging.getLogger(__name__)


def parse_uuid(value: str, param_name: str = "ID") -> UUID:
    """Парсинг UUID с обработкой ошибок."""
    try:
        return UUID(value)
    except ValueError:
        raise web.HTTPBadRequest(text=f"Invalid {param_name} format. Expected UUID.")


def validate_pagination(page: int, page_size: int) -> tuple[int, int]:
    """Валидация параметров пагинации."""
    page = max(1, page or 1)
    page_size = max(1, min(page_size or 50, 100))
    return page, page_size


async def create_run(request: Request) -> web.Response:
    """Создание run."""
    experiment_id = parse_uuid(request.match_info['experiment_id'], "experiment_id")

    # Проверка существования эксперимента
    experiment = await experiment_queries.get_experiment_by_id(experiment_id)
    if not experiment:
        raise web.HTTPNotFound(text=f"Experiment {experiment_id} not found")

    data = await request.json()
    schema = RunCreate(**data)

    # Создание run
    run = await run_queries.create_run(
        experiment_id=experiment_id,
        name=schema.name,
        parameters=schema.parameters,
        notes=schema.notes,
        metadata=schema.metadata
    )

    # Публикация события
    await publish_event("run.created", {
        "run_id": str(run['id']),
        "experiment_id": str(experiment_id)
    })

    return web.json_response(
        RunResponse(**run).dict(),
        status=201
    )


async def get_run(request: Request) -> web.Response:
    """Получение run по ID."""
    run_id = parse_uuid(request.match_info['run_id'], "run_id")

    run = await run_queries.get_run_by_id(run_id)

    if not run:
        raise web.HTTPNotFound(text=f"Run {run_id} not found")

    return web.json_response(RunResponse(**run).dict())


async def list_runs(request: Request) -> web.Response:
    """Список runs для эксперимента."""
    experiment_id = parse_uuid(request.match_info['experiment_id'], "experiment_id")
    status = request.query.get('status')

    try:
        page = int(request.query.get('page', 1))
        page_size = int(request.query.get('page_size', 50))
    except (ValueError, TypeError):
        raise web.HTTPBadRequest(text="Invalid pagination parameters. 'page' and 'page_size' must be integers.")

    # Валидация пагинации
    page, page_size = validate_pagination(page, page_size)

    offset = (page - 1) * page_size

    runs_list, total = await run_queries.list_runs(
        experiment_id=experiment_id,
        status=status,
        limit=page_size,
        offset=offset
    )

    response = RunListResponse(
        runs=[RunResponse(**run) for run in runs_list],
        total=total,
        page=page,
        page_size=page_size
    )

    return web.json_response(response.dict())


async def update_run(request: Request) -> web.Response:
    """Обновление run."""
    run_id = parse_uuid(request.match_info['run_id'], "run_id")

    # Проверка существования
    existing = await run_queries.get_run_by_id(run_id)
    if not existing:
        raise web.HTTPNotFound()

    data = await request.json()
    schema = RunUpdate(**data)

    # Обновление
    run = await run_queries.update_run(
        run_id=run_id,
        name=schema.name,
        parameters=schema.parameters,
        notes=schema.notes,
        metadata=schema.metadata,
        status=schema.status
    )

    # Публикация событий при изменении статуса
    if schema.status:
        if schema.status == 'running' and existing['status'] != 'running':
            await publish_event("run.started", {
                "run_id": str(run_id),
                "experiment_id": str(existing['experiment_id'])
            })
        elif schema.status != existing['status']:
            await publish_event("run.updated", {
                "run_id": str(run_id),
                "old_status": existing['status'],
                "new_status": schema.status
            })

    return web.json_response(RunResponse(**run).dict())


async def complete_run(request: Request) -> web.Response:
    """Завершение run."""
    run_id = parse_uuid(request.match_info['run_id'], "run_id")

    # Проверка существования
    existing = await run_queries.get_run_by_id(run_id)
    if not existing:
        raise web.HTTPNotFound()

    run = await run_queries.complete_run(run_id)

    if not run:
        raise web.HTTPBadRequest(text="Run is not in 'running' status")

    # Публикация события
    await publish_event("run.completed", {
        "run_id": str(run_id),
        "experiment_id": str(existing['experiment_id']),
        "duration_seconds": run['duration_seconds']
    })

    return web.json_response(RunResponse(**run).dict())


async def fail_run(request: Request) -> web.Response:
    """Пометить run как failed."""
    run_id = parse_uuid(request.match_info['run_id'], "run_id")

    # Проверка существования
    existing = await run_queries.get_run_by_id(run_id)
    if not existing:
        raise web.HTTPNotFound()

    data = await request.json() if request.can_read_body else {}
    reason = data.get('reason')

    run = await run_queries.fail_run(run_id, reason=reason)

    # Публикация события
    await publish_event("run.failed", {
        "run_id": str(run_id),
        "experiment_id": str(existing['experiment_id']),
        "reason": reason
    })

    return web.json_response(RunResponse(**run).dict())

