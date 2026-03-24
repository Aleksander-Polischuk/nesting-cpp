cmake_minimum_required(VERSION 3.15)

set(CMAKE_TOOLCHAIN_FILE "C:/Users/user/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "" FORCE)
set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "" FORCE)

project(NestingAPI)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(polyclipping CONFIG REQUIRED)
find_package(NLopt CONFIG REQUIRED)
find_package(Boost REQUIRED)

# Створюємо проект
add_executable(NestingAPI "NestingAPI/NestingAPI.cpp")

# ВКАЗУЄМО ШЛЯХИ (тепер дуже точно)
target_include_directories(NestingAPI PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/NestingAPI
    "C:/Users/user/vcpkg/installed/x64-windows/include"
)

target_link_libraries(NestingAPI PRIVATE 
    nlohmann_json::nlohmann_json 
    polyclipping::polyclipping 
    NLopt::nlopt
)