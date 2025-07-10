cmake_minimum_required(VERSION 3.10)
project(rANS)

# Указываем версию C++ стандарта
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
add_definitions(-D_USE_MATH_DEFINES -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS )
add_compile_options(-mavx -msse4.2 -mssse3 -march=native)

# Добавляем директории для заголовочных файлов
include_directories(include)

# Добавляем исходные файлы
add_executable(rans src/main.cpp include/profiling.hpp include/mio.hpp include/myargs.hpp include/rans.hpp)