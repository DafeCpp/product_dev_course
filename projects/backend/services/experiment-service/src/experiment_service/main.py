"""aiohttp application entrypoint."""
from __future__ import annotations

import asyncio
import hashlib
from pathlib import Path
from typing import Any

import asyncpg
from aiohttp import web
from aiohttp_cors import setup as cors_setup, ResourceOptions

from backend_common.logging_config import configure_logging
from backend_common.middleware.trace import create_trace_middleware

from experiment_service.api.router import setup_routes
from backend_common.db.pool import close_pool_service as close_pool, init_pool_service
from experiment_service.settings import settings


async def init_pool(_app: Any = None) -> None:
    """Initialize pool with service settings."""
    await init_pool_service(_app, settings)

# Configure structured logging
configure_logging()

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
OPENAPI_PATH = PROJECT_ROOT / "openapi" / "openapi.yaml"


async def healthcheck(request: web.Request) -> web.Response:
    return web.json_response({"status": "ok", "service": settings.app_name, "env": settings.env})


async def openapi_spec(request: web.Request) -> web.StreamResponse:
    return web.FileResponse(OPENAPI_PATH, headers={"Content-Type": "application/yaml"})


async def ensure_schema_table(conn: asyncpg.Connection) -> None:
    """Ensure schema_migrations table exists."""
    await conn.execute(
        """
        CREATE TABLE IF NOT EXISTS schema_migrations (
            version text PRIMARY KEY,
            checksum text NOT NULL,
            applied_at timestamptz NOT NULL DEFAULT now()
        );
        """
    )


async def apply_migrations_on_startup(_app: web.Application) -> None:
    """Apply pending migrations on startup."""
    # Try multiple possible paths for migrations directory
    possible_paths = [
        Path(__file__).resolve().parent.parent.parent / "migrations",  # /app/migrations in container
        Path("/app/migrations"),  # Absolute path in container
        Path(__file__).resolve().parent.parent.parent.parent / "migrations",  # Local development
    ]

    migrations_dir = None
    for path in possible_paths:
        if path.exists():
            migrations_dir = path
            break

    if migrations_dir is None:
        print(f"⚠️  Migrations directory not found. Tried: {possible_paths}, skipping migrations")
        return

    # Load migrations
    migrations: dict[str, Path] = {}
    for path in sorted(migrations_dir.glob("*.sql")):
        version = path.stem
        if version in migrations:
            raise ValueError(f"Duplicate migration version detected: {version}")
        migrations[version] = path

    if not migrations:
        print("⚠️  No migrations found, skipping")
        return

    # Connect to database with retry logic
    max_retries = 5
    retry_delay = 2
    conn = None
    for attempt in range(max_retries):
        try:
            conn = await asyncpg.connect(str(settings.database_url))
            break
        except asyncpg.exceptions.InvalidCatalogNameError as e:
            if "does not exist" in str(e):
                print(f"⚠️  Database does not exist yet (attempt {attempt + 1}/{max_retries})")
                if attempt < max_retries - 1:
                    await asyncio.sleep(retry_delay)
                    continue
                print("❌ Database does not exist. Please create it manually or restart PostgreSQL container.")
                return
            raise
        except Exception as e:
            print(f"⚠️  Database connection error (attempt {attempt + 1}/{max_retries}): {e}")
            if attempt < max_retries - 1:
                await asyncio.sleep(retry_delay)
                continue
            print("❌ Failed to connect to database after retries. Skipping migrations.")
            return

    if conn is None:
        print("❌ Failed to establish database connection. Skipping migrations.")
        return

    try:
        await ensure_schema_table(conn)
        rows = await conn.fetch("SELECT version, checksum FROM schema_migrations")
        applied = {row["version"]: row["checksum"] for row in rows}
        pending = []

        for version, path in migrations.items():
            sql = path.read_text(encoding="utf-8")
            checksum = hashlib.sha256(sql.encode("utf-8")).hexdigest()
            if version in applied:
                if applied[version] != checksum:
                    raise RuntimeError(
                        f"Checksum mismatch for {version}: "
                        f"{applied[version]} (db) != {checksum} (file)"
                    )
                continue
            pending.append((version, path, sql, checksum))

        if not pending:
            print("✅ No pending migrations.")
            return

        print(f"🔄 Applying {len(pending)} pending migration(s)...")
        for version, path, sql, checksum in pending:
            print(f"   Applying {path.name}...")
            try:
                async with conn.transaction():
                    await conn.execute(sql)
                    await conn.execute(
                        "INSERT INTO schema_migrations (version, checksum) VALUES ($1, $2)",
                        version,
                        checksum,
                    )
            except asyncpg.exceptions.DuplicateObjectError as e:
                # In test environments, types/tables might already exist from schema files
                # Check if this is a type/object creation error
                if "already exists" in str(e).lower():
                    # Check if migration was already recorded
                    existing = await conn.fetchval(
                        "SELECT version FROM schema_migrations WHERE version = $1", version
                    )
                    if existing:
                        print(f"   ⚠️  Migration {version} already applied, skipping")
                        continue
                    # If not recorded but objects exist, mark as applied (test environment)
                    print(f"   ⚠️  Objects already exist for {version}, marking as applied")
                    # Execute outside transaction since transaction was rolled back
                    await conn.execute(
                        "INSERT INTO schema_migrations (version, checksum) VALUES ($1, $2) ON CONFLICT (version) DO NOTHING",
                        version,
                        checksum,
                    )
                else:
                    raise
        print(f"✅ Applied {len(pending)} migration(s).")
    finally:
        await conn.close()


def create_app() -> web.Application:
    app = web.Application()

    # Add trace middleware first (before other middleware)
    trace_middleware = create_trace_middleware(settings.app_name)
    app.middlewares.append(trace_middleware)

    # Configure CORS first, before adding routes
    cors = cors_setup(
        app,
        defaults={
            origin: ResourceOptions(
                allow_credentials=True,
                expose_headers="*",
                allow_headers="*",
                allow_methods="*",
            )
            for origin in settings.cors_allowed_origins
        },
    )

    app.router.add_get("/health", healthcheck)
    app.router.add_get("/openapi.yaml", openapi_spec)
    setup_routes(app)
    app.on_startup.append(init_pool)
    app.on_startup.append(apply_migrations_on_startup)
    app.on_cleanup.append(close_pool)

    # Add CORS to all routes
    for route in list(app.router.routes()):
        cors.add(route)

    return app


def main() -> None:
    # Disable aiohttp access log (we use structlog middleware instead)
    # This prevents duplicate/unstructured logs
    web.run_app(
        create_app(),
        host=settings.host,
        port=settings.port,
        access_log=None,  # Disable default access log
    )


if __name__ == "__main__":
    main()

