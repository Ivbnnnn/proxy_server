cmake_minimum_required(VERSION 3.10)
project(HttpProxyServer VERSION 0.1 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon REQUIRED CONFIG)
find_package(libpqxx CONFIG QUIET)
if(libpqxx_FOUND)
    set(PROXY_PQXX_TARGET libpqxx::pqxx)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
    set(PROXY_PQXX_TARGET PkgConfig::PQXX)
endif()
find_package(spdlog REQUIRED CONFIG)
find_package(hiredis CONFIG QUIET)
if(hiredis_FOUND)
    set(PROXY_HIREDIS_TARGET hiredis::hiredis)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)
    set(PROXY_HIREDIS_TARGET PkgConfig::HIREDIS)
endif()

find_path(REDIS_PLUS_PLUS_INCLUDE_DIR
    NAMES sw/redis++/redis++.h
    PATH_SUFFIXES include
)
find_library(REDIS_PLUS_PLUS_LIBRARY
    NAMES redis++
)
if(NOT REDIS_PLUS_PLUS_INCLUDE_DIR OR NOT REDIS_PLUS_PLUS_LIBRARY)
    message(FATAL_ERROR "redis-plus-plus headers/library not found")
endif()

set(PROXY_CORE_SOURCES
    src/Types.cpp
    src/AdminUser.cpp
    src/WhitelistRule.cpp
    src/CacheEntry.cpp
    src/RequestRecord.cpp
    src/AppLogger.cpp
    src/DBService.cpp
    src/RedisService.cpp
    src/FilterService.cpp
    src/CacheService.cpp
    src/ProxyController.cpp
    src/AdminUI.cpp
)

add_library(proxy_core STATIC ${PROXY_CORE_SOURCES})

target_include_directories(proxy_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${REDIS_PLUS_PLUS_INCLUDE_DIR}
)

if(MSVC)
    target_compile_definitions(proxy_core PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(proxy_core
    PUBLIC
        Drogon::Drogon
        ${REDIS_PLUS_PLUS_LIBRARY}
        ${PROXY_HIREDIS_TARGET}
        ${PROXY_PQXX_TARGET}
        spdlog::spdlog
)

add_executable(http_proxy_server src/main.cpp)
target_link_libraries(http_proxy_server PRIVATE proxy_core)

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    set(PROXY_TEST_SUPPORT
        tests/support/TestSupport.cpp
    )

    add_executable(proxy_unit_tests
        tests/unit/proxy_unit_tests.cpp
        ${PROXY_TEST_SUPPORT}
    )
    target_include_directories(proxy_unit_tests PRIVATE tests/support)
    target_link_libraries(proxy_unit_tests PRIVATE proxy_core GTest::gtest_main)
    gtest_add_tests(
        TARGET proxy_unit_tests
        TEST_LIST PROXY_UNIT_TESTS
    )
    set_tests_properties(${PROXY_UNIT_TESTS} PROPERTIES LABELS "unit;postgres;redis")

    function(add_proxy_scenario target source labels)
        add_executable(${target}
            ${source}
            ${PROXY_TEST_SUPPORT}
        )
        target_include_directories(${target} PRIVATE tests/support)
        target_link_libraries(${target} PRIVATE proxy_core)
        add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
        set_tests_properties(${target} PROPERTIES LABELS "${labels}")
    endfunction()

    add_proxy_scenario(scenario_whitelist_crud tests/scenarios/scenario_whitelist_crud.cpp "scenario;postgres")
    add_proxy_scenario(scenario_proxy_cache tests/scenarios/scenario_proxy_cache.cpp "scenario;postgres;redis")
    add_proxy_scenario(scenario_block_and_log tests/scenarios/scenario_block_and_log.cpp "scenario;postgres;redis")
    add_proxy_scenario(scenario_cache_admin_flow tests/scenarios/scenario_cache_admin_flow.cpp "scenario;redis")
    add_proxy_scenario(scenario_export_requests tests/scenarios/scenario_export_requests.cpp "scenario;postgres")
endif()
