#!/usr/bin/env sh
set -eu

service postgresql start
su postgres -c "psql -c \"ALTER USER postgres PASSWORD 'postgres';\"" >/dev/null

DB_NAME="${PROXY_DB_NAME:-proxy_db}"
if ! su postgres -c "psql -tAc \"SELECT 1 FROM pg_database WHERE datname='${DB_NAME}'\"" | grep -q 1; then
    su postgres -c "createdb '${DB_NAME}'"
fi

redis-server --daemonize yes

exec "$@"
