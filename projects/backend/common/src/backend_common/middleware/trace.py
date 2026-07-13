"""Middleware for trace_id and request_id logging."""

from __future__ import annotations

import time
from uuid import UUID, uuid4

import structlog
from aiohttp import web

TRACE_ID_HEADER = "X-Trace-Id"
REQUEST_ID_HEADER = "X-Request-Id"

logger = structlog.get_logger(__name__)

# Заголовки, которые не нужно логировать (чувствительные данные)
SENSITIVE_HEADERS = {
    "authorization",
    "cookie",
    "set-cookie",
    "x-api-key",
    "x-auth-token",
    "x-sensor-token",
}


def is_valid_uuid(value: str | None) -> bool:
    """Check if string is a valid UUID."""
    try:
        UUID(value)
        return True
    except (ValueError, TypeError, AttributeError):
        return False


def get_safe_headers(headers) -> dict:
    """Get headers dict with sensitive headers filtered out."""
    safe_headers = {}
    # Преобразуем MultiDictProxy или dict в обычный dict
    headers_dict = dict(headers.items()) if hasattr(headers, "items") else headers

    for key, value in headers_dict.items():
        if key.lower() not in SENSITIVE_HEADERS:
            # Если значение - список или итерируемый объект (но не строка), берем первое значение
            if hasattr(value, "__iter__") and not isinstance(value, (str, bytes)):
                values_list = list(value)
                if len(values_list) == 1:
                    safe_headers[key] = values_list[0]
                elif len(values_list) > 1:
                    safe_headers[key] = values_list[:3]  # Ограничиваем количество значений
            else:
                safe_headers[key] = value
    return safe_headers


def create_trace_middleware(service_name: str):
    """Create trace middleware with specified service name."""

    @web.middleware
    async def trace_middleware(request: web.Request, handler):
        """Middleware to extract and log trace_id and request_id."""
        # Засекаем время начала обработки запроса
        start_time = time.time()

        # Extract or generate trace_id
        trace_id = request.headers.get(TRACE_ID_HEADER)
        if not trace_id or not is_valid_uuid(trace_id):
            trace_id = str(uuid4())

        # Extract or generate request_id
        request_id = request.headers.get(REQUEST_ID_HEADER)
        if not request_id or not is_valid_uuid(request_id):
            request_id = str(uuid4())

        # Store in request for use in handlers
        request["trace_id"] = trace_id
        request["request_id"] = request_id

        # Configure structlog context for this request
        structlog.contextvars.clear_contextvars()
        structlog.contextvars.bind_contextvars(
            trace_id=trace_id,
            request_id=request_id,
            service=service_name,
            method=request.method,
            path=request.path,
        )

        # Подготавливаем информацию о запросе для логирования
        request_info: dict[str, object] = {
            "method": request.method,
            "url": str(request.url),
            "path": request.path,
            "query_string": str(request.query_string) if request.query_string else None,
            "remote": request.remote if hasattr(request, "remote") else None,
            "headers": get_safe_headers(dict(request.headers)),
        }

        # Пытаемся получить размер тела запроса
        if request.content_length:
            request_info["content_length"] = request.content_length

        # Логируем входящий запрос
        logger.info("Incoming request", **request_info)

        try:
            # Выполняем обработчик
            response = await handler(request)

            # Вычисляем время выполнения
            duration_ms = (time.time() - start_time) * 1000

            # Подготавливаем информацию об ответе
            response_info = {
                "method": request.method,
                "url": str(request.url),
                "path": request.path,
                "status_code": response.status,
                "duration_ms": round(duration_ms, 2),
                "response_headers": get_safe_headers(dict(response.headers)),
            }

            # Пытаемся получить размер тела ответа
            content_length = response.headers.get("Content-Length")
            if content_length:
                try:
                    response_info["content_length"] = int(content_length)
                except (ValueError, TypeError):
                    pass

            # Логируем успешный ответ
            if response.status >= 400:
                logger.warn("Request completed with error status", **response_info)
            else:
                logger.info("Request completed", **response_info)

            # Add trace_id and request_id to response headers
            response.headers[TRACE_ID_HEADER] = trace_id
            response.headers[REQUEST_ID_HEADER] = request_id

            return response
        except web.HTTPException as e:
            # Обрабатываем HTTP исключения (4xx, 5xx)
            duration_ms = (time.time() - start_time) * 1000

            logger.warn(
                "Request failed with HTTP exception",
                method=request.method,
                url=str(request.url),
                path=request.path,
                status_code=e.status_code,
                duration_ms=round(duration_ms, 2),
                error=str(e),
                error_type=type(e).__name__,
                exc_info=True,  # Включаем полный traceback
            )
            raise
        except Exception as e:
            # Обрабатываем все остальные исключения
            duration_ms = (time.time() - start_time) * 1000

            logger.error(
                "Request failed with exception",
                method=request.method,
                url=str(request.url),
                path=request.path,
                duration_ms=round(duration_ms, 2),
                error=str(e),
                error_type=type(e).__name__,
                exc_info=True,  # Включаем полный traceback
            )
            raise
        finally:
            # Clear context vars after request
            structlog.contextvars.clear_contextvars()

    return trace_middleware
