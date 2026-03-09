cmake_minimum_required(VERSION 3.16)
project(LibraryManagementSystem VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# Collect source files
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
)

# Create executable
add_executable(lms ${SOURCES})

# Include directories
target_include_directories(lms PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${SQLite3_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(lms PRIVATE
    SQLite::SQLite3
    Threads::Threads
)

# macOS specific
if(APPLE)
    target_link_libraries(lms PRIVATE "-framework CoreFoundation" "-framework Security")
endif()

# Copy static files to build directory
add_custom_command(TARGET lms POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/static ${CMAKE_BINARY_DIR}/static
    COMMENT "Copying static files to build directory"
)

# Copy schema to build directory
add_custom_command(TARGET lms POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/src/db/schema.sql ${CMAKE_BINARY_DIR}/schema.sql
    COMMENT "Copying schema.sql to build directory"
)
