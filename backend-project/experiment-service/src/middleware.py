"""Middleware для приложения."""
import logging
import json
from uuid import UUID
from aiohttp import web
from aiohttp.web_request import Request

from src.config import settings

logger = logging.getLogger(__name__)


async def auth_middleware(request: Request, handler):
    """Middleware для проверки аутентификации."""
    # Публичные endpoints
    public_paths = ['/health']

    if request.path in public_paths:
        return await handler(request)

    # Проверка токена
    auth_header = request.headers.get('Authorization')
    if not auth_header or not auth_header.startswith('Bearer '):
        raise web.HTTPUnauthorized(text="Missing or invalid Authorization header")

    token = auth_header.replace('Bearer ', '')

    # Валидация токена через Auth Service
    # ВРЕМЕННОЕ РЕШЕНИЕ ДЛЯ DEVELOPMENT
    # В продакшене нужно валидировать через Auth Service
    try:
        if settings.DEBUG or settings.ENV == "development":
            # Для development: пытаемся извлечь user_id из токена (если это UUID)
            # Если токен не UUID, используем дефолтный тестовый ID
            try:
                user_id = UUID(token)
                request['user_id'] = user_id
                logger.debug(f"Development mode: using user_id from token: {user_id}")
            except ValueError:
                # Если токен не UUID, используем дефолтный тестовый ID для development
                default_user_id = UUID('00000000-0000-0000-0000-000000000001')
                request['user_id'] = default_user_id
                logger.debug(f"Development mode: using default user_id: {default_user_id}")
        else:
            # В продакшене - валидация через Auth Service
            # TODO: Реализовать реальную валидацию через Auth Service
            # async with httpx.AsyncClient() as client:
            #     response = await client.get(
            #         f"{settings.AUTH_SERVICE_URL}/verify",
            #         headers={"Authorization": f"Bearer {token}"}
            #     )
            #     if response.status_code != 200:
            #         raise web.HTTPUnauthorized(text="Invalid token")
            #     user_data = response.json()
            #     request['user_id'] = UUID(user_data['user_id'])

            # Пока Auth Service не реализован, выдаем ошибку в production
            logger.warning("Auth Service not implemented, but not in DEBUG mode")
            raise web.HTTPUnauthorized(text="Auth Service not implemented. Use DEBUG=true for development")

    except web.HTTPUnauthorized:
        raise
    except Exception as e:
        logger.error(f"Auth validation failed: {e}", exc_info=True)
        raise web.HTTPUnauthorized(text="Invalid token")

    return await handler(request)


async def error_middleware(request: Request, handler):
    """Middleware для обработки ошибок."""
    try:
        response = await handler(request)
        return response
    except web.HTTPException:
        raise
    except Exception as e:
        logger.error(f"Unhandled error: {e}", exc_info=True)
        return web.json_response(
            {"error": "Internal server error"},
            status=500
        )

