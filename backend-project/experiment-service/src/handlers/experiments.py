"""Обработчики для экспериментов."""
import logging
from uuid import UUID
from aiohttp import web
from aiohttp.web_request import Request

from src.schemas import (
    ExperimentCreate,
    ExperimentUpdate,
    ExperimentResponse,
    ExperimentListResponse
)
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


async def create_experiment(request: Request) -> web.Response:
    """Создание эксперимента."""
    user_id = request.get('user_id')  # Из middleware
    if not user_id:
        raise web.HTTPUnauthorized()

    data = await request.json()
    schema = ExperimentCreate(**data)

    # Создание эксперимента
    experiment = await experiment_queries.create_experiment(
        project_id=schema.project_id,
        name=schema.name,
        created_by=user_id,
        description=schema.description,
        experiment_type=schema.experiment_type,
        tags=schema.tags,
        metadata=schema.metadata
    )

    # Публикация события
    await publish_event("experiment.created", {
        "experiment_id": str(experiment['id']),
        "project_id": str(experiment['project_id']),
        "created_by": str(user_id)
    })

    return web.json_response(
        ExperimentResponse(**experiment).dict(),
        status=201
    )


async def get_experiment(request: Request) -> web.Response:
    """Получение эксперимента по ID."""
    experiment_id = parse_uuid(request.match_info['experiment_id'], "experiment_id")

    experiment = await experiment_queries.get_experiment_by_id(experiment_id)

    if not experiment:
        raise web.HTTPNotFound(text=f"Experiment {experiment_id} not found")

    return web.json_response(ExperimentResponse(**experiment).dict())


async def list_experiments(request: Request) -> web.Response:
    """Список экспериментов."""
    # Парсинг query параметров
    project_id = request.query.get('project_id')
    status = request.query.get('status')
    tags = request.query.get('tags')  # comma-separated

    try:
        page = int(request.query.get('page', 1))
        page_size = int(request.query.get('page_size', 50))
    except (ValueError, TypeError):
        raise web.HTTPBadRequest(text="Invalid pagination parameters. 'page' and 'page_size' must be integers.")

    # Валидация пагинации
    page, page_size = validate_pagination(page, page_size)

    offset = (page - 1) * page_size

    # Конвертация типов
    try:
        project_uuid = UUID(project_id) if project_id else None
    except ValueError:
        raise web.HTTPBadRequest(text="Invalid project_id format. Expected UUID.")
    tags_list = tags.split(',') if tags else None

    experiments_list, total = await experiment_queries.list_experiments(
        project_id=project_uuid,
        status=status,
        tags=tags_list,
        limit=page_size,
        offset=offset
    )

    response = ExperimentListResponse(
        experiments=[ExperimentResponse(**exp) for exp in experiments_list],
        total=total,
        page=page,
        page_size=page_size
    )

    return web.json_response(response.dict())


async def update_experiment(request: Request) -> web.Response:
    """Обновление эксперимента."""
    experiment_id = parse_uuid(request.match_info['experiment_id'], "experiment_id")
    user_id = request.get('user_id')

    # Проверка существования
    existing = await experiment_queries.get_experiment_by_id(experiment_id)
    if not existing:
        raise web.HTTPNotFound()

    data = await request.json()
    schema = ExperimentUpdate(**data)

    # Обновление
    experiment = await experiment_queries.update_experiment(
        experiment_id=experiment_id,
        name=schema.name,
        description=schema.description,
        experiment_type=schema.experiment_type,
        tags=schema.tags,
        metadata=schema.metadata,
        status=schema.status
    )

    # Публикация события при изменении статуса
    if schema.status and schema.status != existing['status']:
        await publish_event("experiment.updated", {
            "experiment_id": str(experiment_id),
            "old_status": existing['status'],
            "new_status": schema.status
        })

    return web.json_response(ExperimentResponse(**experiment).dict())


async def delete_experiment(request: Request) -> web.Response:
    """Удаление эксперимента."""
    experiment_id = parse_uuid(request.match_info['experiment_id'], "experiment_id")

    # Проверка существования
    existing = await experiment_queries.get_experiment_by_id(experiment_id)
    if not existing:
        raise web.HTTPNotFound()

    deleted = await experiment_queries.delete_experiment(experiment_id)

    if deleted:
        await publish_event("experiment.deleted", {
            "experiment_id": str(experiment_id),
            "project_id": str(existing['project_id'])
        })
        return web.json_response({"message": "Experiment deleted"})
    else:
        raise web.HTTPInternalServerError(text="Failed to delete experiment")


async def search_experiments(request: Request) -> web.Response:
    """Поиск экспериментов."""
    query = request.query.get('q')
    project_id = request.query.get('project_id')

    try:
        page = int(request.query.get('page', 1))
        page_size = int(request.query.get('page_size', 50))
    except (ValueError, TypeError):
        raise web.HTTPBadRequest(text="Invalid pagination parameters. 'page' and 'page_size' must be integers.")

    # Валидация пагинации
    page, page_size = validate_pagination(page, page_size)

    offset = (page - 1) * page_size

    # Конвертация типов
    try:
        project_uuid = UUID(project_id) if project_id else None
    except ValueError:
        raise web.HTTPBadRequest(text="Invalid project_id format. Expected UUID.")

    experiments_list, total = await experiment_queries.search_experiments(
        query=query,
        project_id=project_uuid,
        limit=page_size,
        offset=offset
    )

    response = ExperimentListResponse(
        experiments=[ExperimentResponse(**exp) for exp in experiments_list],
        total=total,
        page=page,
        page_size=page_size
    )

    return web.json_response(response.dict())

