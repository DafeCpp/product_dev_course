#!/bin/bash
set -e

# Default passwords for database users (can be overridden via environment variables)
AUTH_DB_USER=${AUTH_DB_USER:-auth_user}
AUTH_DB_PASSWORD=${AUTH_DB_PASSWORD:-auth_password}
EXPERIMENT_DB_USER=${EXPERIMENT_DB_USER:-experiment_user}
EXPERIMENT_DB_PASSWORD=${EXPERIMENT_DB_PASSWORD:-experiment_password}

echo "Creating databases and users..."

# Create auth_db and auth_user
echo "Creating database: auth_db"
psql -v ON_ERROR_STOP=0 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" -c "CREATE DATABASE auth_db;" 2>&1 | grep -v "already exists" || true

echo "Creating user: $AUTH_DB_USER"
psql -v ON_ERROR_STOP=0 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
    DO \$\$
    BEGIN
        IF NOT EXISTS (SELECT FROM pg_user WHERE usename = '$AUTH_DB_USER') THEN
            CREATE USER $AUTH_DB_USER WITH PASSWORD '$AUTH_DB_PASSWORD';
        ELSE
            ALTER USER $AUTH_DB_USER WITH PASSWORD '$AUTH_DB_PASSWORD';
        END IF;
    END
    \$\$;
EOSQL

echo "Granting privileges on auth_db to $AUTH_DB_USER"
psql -v ON_ERROR_STOP=0 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
    GRANT ALL PRIVILEGES ON DATABASE auth_db TO $AUTH_DB_USER;
EOSQL

# Grant privileges on auth_db schema and create pgcrypto (superuser only)
psql -v ON_ERROR_STOP=0 --username "$POSTGRES_USER" --dbname "auth_db" <<-EOSQL
    CREATE EXTENSION IF NOT EXISTS pgcrypto;
    GRANT ALL ON SCHEMA public TO $AUTH_DB_USER;
    ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT ALL ON TABLES TO $AUTH_DB_USER;
    ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT ALL ON SEQUENCES TO $AUTH_DB_USER;
EOSQL

# Create experiment_user for experiment_db
echo "Creating user: $EXPERIMENT_DB_USER"
psql -v ON_ERROR_STOP=0 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
    DO \$\$
    BEGIN
        IF NOT EXISTS (SELECT FROM pg_user WHERE usename = '$EXPERIMENT_DB_USER') THEN
            CREATE USER $EXPERIMENT_DB_USER WITH PASSWORD '$EXPERIMENT_DB_PASSWORD';
        ELSE
            ALTER USER $EXPERIMENT_DB_USER WITH PASSWORD '$EXPERIMENT_DB_PASSWORD';
        END IF;
    END
    \$\$;
EOSQL

echo "Granting privileges on experiment_db to $EXPERIMENT_DB_USER"
psql -v ON_ERROR_STOP=0 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
    GRANT ALL PRIVILEGES ON DATABASE experiment_db TO $EXPERIMENT_DB_USER;
EOSQL

# Grant privileges on experiment_db schema and create pgcrypto (superuser only)
psql -v ON_ERROR_STOP=0 --username "$POSTGRES_USER" --dbname "experiment_db" <<-EOSQL
    CREATE EXTENSION IF NOT EXISTS pgcrypto;
    GRANT ALL ON SCHEMA public TO $EXPERIMENT_DB_USER;
    ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT ALL ON TABLES TO $EXPERIMENT_DB_USER;
    ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT ALL ON SEQUENCES TO $EXPERIMENT_DB_USER;
EOSQL

echo "✅ Multiple databases and users initialized"

