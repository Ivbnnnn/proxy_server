FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        default-libmysqlclient-dev \
        git \
        libbrotli-dev \
        libdrogon-dev \
        libgtest-dev \
        libhiredis-dev \
        libjsoncpp-dev \
        libpqxx-dev \
        libsqlite3-dev \
        libspdlog-dev \
        libyaml-cpp-dev \
        ninja-build \
        pkg-config \
        postgresql \
        redis-server \
        uuid-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

RUN sed -i 's/#  define pqxx_have_source_location 1/#  define pqxx_have_source_location 0/' \
    /usr/include/pqxx/internal/cxx-features.hxx

RUN git clone --depth 1 --branch 1.3.15 https://github.com/sewenew/redis-plus-plus.git /tmp/redis-plus-plus \
    && cmake -S /tmp/redis-plus-plus -B /tmp/redis-plus-plus/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DREDIS_PLUS_PLUS_BUILD_TEST=OFF \
        -DREDIS_PLUS_PLUS_CXX_STANDARD=17 \
    && cmake --build /tmp/redis-plus-plus/build \
    && cmake --install /tmp/redis-plus-plus/build \
    && ldconfig \
    && rm -rf /tmp/redis-plus-plus

WORKDIR /app

COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
COPY tests ./tests

ENV PROXY_TEST_DB_CONN="host=127.0.0.1 port=5432 dbname=proxy_test_db user=postgres password=postgres" \
    PROXY_TEST_REDIS_HOST=127.0.0.1 \
    PROXY_TEST_REDIS_PORT=6379 \
    PROXY_TEST_REDIS_DB=15

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DMYSQL_INCLUDE_DIRS=/usr/include/mysql \
        -DMYSQL_LIBRARIES=/usr/lib/x86_64-linux-gnu/libmysqlclient.so \
    && cmake --build build

FROM build AS test

RUN service postgresql start \
    && su postgres -c "psql -c \"ALTER USER postgres PASSWORD 'postgres';\"" \
    && redis-server --daemonize yes \
    && ctest --test-dir build --output-on-failure

FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libdrogon1t64 \
        libhiredis1.1.0 \
        libpqxx-7.8t64 \
        libspdlog1.12 \
        postgresql \
        redis-server \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=build /app/build/http_proxy_server ./http_proxy_server
COPY --from=build /usr/local/lib/libredis++.so* /usr/local/lib/
COPY docker-entrypoint.sh /usr/local/bin/proxy-entrypoint

RUN chmod +x /usr/local/bin/proxy-entrypoint \
    && ldconfig

ENV PROXY_DB_CONN="host=127.0.0.1 port=5432 dbname=proxy_db user=postgres password=postgres" \
    PROXY_REDIS_HOST=127.0.0.1 \
    PROXY_REDIS_PORT=6379

EXPOSE 8080

ENTRYPOINT ["proxy-entrypoint"]
CMD ["./http_proxy_server"]
