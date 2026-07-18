cmake_minimum_required(VERSION 3.12)
project(Infotecs_File_Transfer CXX)

# Требуем стандарт C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Папка, куда CMake будет складывать скомпилированные исполняемые файлы и библиотеки
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Cобираем динамические библиотеки
# Собираем libfileop.so (Раннее связывание)
add_library(fileop SHARED libs/fileop_lib/fileop.cpp)

# Собираем libprogress.so (Позднее связывание)
add_library(progress SHARED libs/progress_lib/progress.cpp)

# Сборка сервера
add_executable(server_app server/server.cpp)

# Подключаем пути к заголовочным файлам для сервера
target_include_directories(server_app PRIVATE 
    common
    libs/fileop_lib
)

# Связываем сервер с библиотекой файловых операций (Раннее связывание)
# Также подключаем системную библиотеку pthread для работы с потоками
target_link_libraries(server_app PRIVATE fileop pthread)

# Сборка клиента
add_executable(client_app client/client.cpp)

# Подключаем пути к заголовочным файлам для клиента
target_include_directories(client_app PRIVATE 
    common    
    libs/fileop_lib
)

# Связываем клиент с библиотекой файловых операций (Раннее связывание)
# И подключаем системную библиотеку dl для позднего связывания
target_link_libraries(client_app PRIVATE fileop dl)