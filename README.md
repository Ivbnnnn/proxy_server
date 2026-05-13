## Build

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/Users/user/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build
```

## Run

powershell
cd "C:\Users\user\Desktop\proxy server"
.\build\Debug\http_proxy_server.exe

## Frontend

Локальная сборка
cd "C:\Users\user\Desktop\proxy server\frontend"
npm install
npm run dev

Продакшн билд:
npm run build


## Testing
Build and run tests:

powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/Users/user/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build
ctest --test-dir build -C Debug --output-on-failure


powershell
ctest --test-dir build -L unit --output-on-failure
ctest --test-dir build -L scenario --output-on-failure
ctest --test-dir build -L postgres --output-on-failure
ctest --test-dir build -L redis --output-on-failure


## Docker commands
Проверка что корректно билдится
docker build --target test -t proxy-server:test .

Сборка билда
docker build -t proxy-server:latest .

Запуск контейнера
docker run -d --name proxy-server-check -p 8080:8080 proxy-server:latest

## Environment Variables

- `PROXY_DB_CONN` (default: `host=127.0.0.1 port=5432 dbname=proxy_db user=postgres password=postgres`)
- `PROXY_REDIS_HOST` (default: `127.0.0.1`)
- `PROXY_REDIS_PORT` (default: `6379`)
