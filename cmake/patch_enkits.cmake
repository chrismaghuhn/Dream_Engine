file(READ "CMakeLists.txt" _enki_cmake)
string(REGEX REPLACE "cmake_minimum_required\\(VERSION [0-9.]+\\)"
       "cmake_minimum_required(VERSION 3.16)" _enki_cmake "${_enki_cmake}")
file(WRITE "CMakeLists.txt" "${_enki_cmake}")
