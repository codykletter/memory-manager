cmake_minimum_required(VERSION 4.2)
project(memory_manager C)

set(CMAKE_C_STANDARD 11)

include_directories(.)
include_directories(data_structures)

add_executable(memory_manager
        data_structures/pfn.c
        data_structures/pfn.h
        data_structures/pte.c
        data_structures/pte.h
        data_structures/pagefile.c
        data_structures/pagefile.h
        cody_vm.c
        macros.h
        pages.h
        pages.c
)
