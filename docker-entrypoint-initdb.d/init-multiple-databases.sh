#!/bin/bash
set -e

# Create auth_db if it doesn't exist
psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
    SELECT 'CREATE DATABASE auth_db'
    WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'auth_db')\gexec
EOSQL

echo "Multiple databases created"

