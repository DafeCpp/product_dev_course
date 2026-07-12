#!/usr/bin/env python3
"""Seed database with initial admin user if it doesn't exist."""
# pyright: reportMissingImports=false
from __future__ import annotations

import argparse
import asyncio
import os
import sys
from typing import Optional

import asyncpg
import bcrypt


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Seed initial admin user.")
    parser.add_argument(
        "--database-url",
        "-d",
        default=os.getenv("AUTH_DATABASE_URL"),
        help="PostgreSQL connection string. Defaults to AUTH_DATABASE_URL env variable.",
    )
    parser.add_argument(
        "--username",
        default=os.getenv("ADMIN_USERNAME", "admin"),
        help="Admin username (default: 'admin', env: ADMIN_USERNAME).",
    )
    parser.add_argument(
        "--email",
        default=os.getenv("ADMIN_EMAIL", "admin@example.com"),
        help="Admin email (default: 'admin@example.com', env: ADMIN_EMAIL).",
    )
    parser.add_argument(
        "--password",
        default=os.getenv("ADMIN_PASSWORD"),
        help="Admin password (required, env: ADMIN_PASSWORD). For security, use env variable.",
    )
    parser.add_argument(
        "--skip-if-exists",
        action="store_true",
        default=True,
        help="Skip if any admin/superadmin already exists (default: true).",
    )
    return parser.parse_args()


def hash_password(password: str) -> str:
    """Hash password using bcrypt with 12 rounds."""
    salt = bcrypt.gensalt(rounds=12)
    hashed = bcrypt.hashpw(password.encode("utf-8"), salt)
    return hashed.decode("utf-8")


async def admin_exists(conn: asyncpg.Connection) -> bool:
    """Check if any admin or superadmin user already exists."""
    query = """
        SELECT EXISTS(
            SELECT 1 FROM users u
            INNER JOIN user_system_roles usr ON u.id = usr.user_id
            INNER JOIN roles r ON usr.role_id = r.id
            WHERE r.name IN ('admin', 'superadmin') AND u.is_active = true
            LIMIT 1
        )
    """
    row = await conn.fetchrow(query)
    return bool(row["exists"]) if row else False


async def create_admin_user(
    conn: asyncpg.Connection,
    username: str,
    email: str,
    hashed_password: str,
) -> str:
    """Create admin user and assign admin role. Returns user ID."""
    # Check if user with same username/email exists
    exists_query = """
        SELECT id FROM users WHERE username = $1 OR email = $2
    """
    existing = await conn.fetchrow(exists_query, username, email)
    if existing:
        raise RuntimeError(
            f"User with username '{username}' or email '{email}' already exists"
        )

    # Create user
    create_query = """
        INSERT INTO users (username, email, hashed_password, is_active)
        VALUES ($1, $2, $3, true)
        RETURNING id
    """
    user_row = await conn.fetchrow(
        create_query, username, email, hashed_password
    )
    if not user_row:
        raise RuntimeError("Failed to create user")

    user_id = user_row["id"]

    # Assign admin role (UUID: 00000000-0000-0000-0000-000000000002)
    # granted_by is self-assigned during seed
    assign_query = """
        INSERT INTO user_system_roles (user_id, role_id, granted_by)
        VALUES ($1, $2, $1)
        ON CONFLICT (user_id, role_id) DO NOTHING
    """
    admin_role_id = "00000000-0000-0000-0000-000000000002"
    await conn.execute(assign_query, user_id, admin_role_id)

    return user_id


async def seed_admin(
    database_url: str,
    username: str,
    email: str,
    password: str,
    skip_if_exists: bool,
) -> None:
    """Seed database with initial admin user."""
    conn = await asyncpg.connect(database_url)
    try:
        # Check if admin already exists
        if skip_if_exists and await admin_exists(conn):
            print("✓ Admin user already exists, skipping seed.")
            return

        # Validate inputs
        if not username or not email or not password:
            raise ValueError(
                "username, email, and password must be provided"
            )

        if len(password) < 8:
            raise ValueError("Password must be at least 8 characters")

        # Create admin user
        hashed = hash_password(password)
        async with conn.transaction():
            user_id = await create_admin_user(conn, username, email, hashed)

        print(f"✓ Admin user created:")
        print(f"  username: {username}")
        print(f"  email: {email}")
        print(f"  user_id: {user_id}")
        print(f"  role: admin")

    finally:
        await conn.close()


async def main_async() -> None:
    args = parse_args()

    if not args.database_url:
        print(
            "Error: Database URL must be provided via --database-url or "
            "AUTH_DATABASE_URL env variable.",
            file=sys.stderr,
        )
        sys.exit(1)

    if not args.password and not args.skip_if_exists:
        print(
            "Error: --password or ADMIN_PASSWORD env variable is required "
            "when not using --skip-if-exists.",
            file=sys.stderr,
        )
        sys.exit(1)

    if args.password:
        await seed_admin(
            args.database_url,
            args.username,
            args.email,
            args.password,
            args.skip_if_exists,
        )
    else:
        print("✓ No password provided, skipping admin seed (admin may already exist).")


def main() -> None:
    asyncio.run(main_async())


if __name__ == "__main__":
    main()
