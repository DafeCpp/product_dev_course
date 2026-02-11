"""Logging configuration for structured TSKV (Tab-Separated Key-Value) logging."""
from __future__ import annotations

import logging
import sys

import structlog


def _sanitize_string(value: str) -> str:
    """Escape control characters so the log entry stays on a single line."""
    return (
        value.replace("\\", "\\\\")
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )


def replace_newlines_processor(logger, method_name, event_dict):
    """
    Processor to replace newlines in string values with \\n.
    This ensures each log entry stays on a single line for TSKV format.
    Processes all string values including those added by format_exc_info.
    """
    for key, value in event_dict.items():
        if isinstance(value, str):
            event_dict[key] = _sanitize_string(value)
        elif isinstance(value, (list, tuple)):
            event_dict[key] = [
                _sanitize_string(item) if isinstance(item, str) else item
                for item in value
            ]
        elif isinstance(value, dict):
            event_dict[key] = {
                k: _sanitize_string(v) if isinstance(v, str) else v
                for k, v in value.items()
            }
    return event_dict


class SingleLineFormatter(logging.Formatter):
    """Formatter that ensures output is always on a single line."""

    def format(self, record):
        # Get the formatted message
        message = super().format(record)
        # Replace any remaining newlines with \\n
        # This catches any newlines that might have been introduced
        return message.replace("\n", "\\n").replace("\r", "\\r")


def configure_logging() -> None:
    """Configure structlog for TSKV output suitable for Grafana/Loki/Alloy."""
    # Configure standard logging to pass through to structlog
    # This ensures all logs (including aiohttp) go through structlog
    # Use a handler that ensures single-line output
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(SingleLineFormatter("%(message)s"))

    # Configure root logger
    root_logger = logging.getLogger()
    root_logger.handlers = [handler]
    root_logger.setLevel(logging.INFO)
    root_logger.propagate = False

    # Configure aiohttp logger to use structlog
    aiohttp_logger = logging.getLogger("aiohttp.access")
    aiohttp_logger.setLevel(logging.INFO)
    aiohttp_logger.propagate = True  # Let it go through root logger to structlog
    aiohttp_logger.handlers = []  # Remove default handlers

    # Configure structlog with TSKV format (key=value)
    # Format: timestamp=2024-01-01T12:00:00Z level=INFO logger=service event=message trace_id=123 request_id=456 path=/api/v1
    # Values with spaces are automatically quoted: path="/api/v1/users"
    # Alloy/Loki can easily parse this format and extract fields as labels
    structlog.configure(
        processors=[
            # Merge context variables (trace_id, request_id, etc.)
            structlog.contextvars.merge_contextvars,
            # Add log level
            structlog.stdlib.add_log_level,
            # Add logger name
            structlog.stdlib.add_logger_name,
            # Add timestamp
            structlog.processors.TimeStamper(fmt="iso"),
            # Add stack info for exceptions
            structlog.processors.StackInfoRenderer(),
            # Format exceptions (adds exception field with traceback)
            structlog.processors.format_exc_info,
            # Replace newlines in string values with \\n to keep logs on single line
            # This MUST be after format_exc_info to process the formatted traceback
            # and before KeyValueRenderer
            replace_newlines_processor,
            # KeyValueRenderer outputs key=value format (space-separated)
            # This format is easier for Alloy to parse than JSON
            # Values with spaces are automatically quoted
            structlog.processors.KeyValueRenderer(
                key_order=["timestamp", "level", "logger", "event", "message"],
                drop_missing=True,
            ),
        ],
        wrapper_class=structlog.stdlib.BoundLogger,
        context_class=dict,
        logger_factory=structlog.stdlib.LoggerFactory(),
        cache_logger_on_first_use=True,
    )

