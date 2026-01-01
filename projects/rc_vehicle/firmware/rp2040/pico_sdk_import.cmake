# Это файл для импорта Pico SDK
# Если PICO_SDK_PATH не установлен, SDK будет загружен автоматически
# Или установите переменную окружения PICO_SDK_PATH

if (NOT PICO_SDK_PATH)
    if (PICO_SDK_FETCH_FROM_GIT)
        include(FetchContent)
        FetchContent_Declare(
            pico_sdk
            GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk.git
            GIT_TAG master
            GIT_SUBMODULES ""  # FetchContent не поддерживает рекурсивные подмодули
        )
        FetchContent_Populate(pico_sdk)
        set(PICO_SDK_PATH ${pico_sdk_SOURCE_DIR})
        # Загрузка подмодулей вручную
        find_package(Git QUIET)
        if(GIT_FOUND AND EXISTS "${PICO_SDK_PATH}/.git")
            execute_process(
                COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                WORKING_DIRECTORY ${PICO_SDK_PATH}
                RESULT_VARIABLE GIT_SUBMOD_RESULT
            )
            if(NOT GIT_SUBMOD_RESULT EQUAL "0")
                message(WARNING "Не удалось загрузить подмодули Pico SDK. Попробуйте выполнить вручную: cd ${PICO_SDK_PATH} && git submodule update --init --recursive")
            endif()
        endif()
    elseif(PICO_SDK_FETCH_FROM_GIT_TAG)
        include(FetchContent)
        FetchContent_Declare(
            pico_sdk
            GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk.git
            GIT_TAG ${PICO_SDK_FETCH_FROM_GIT_TAG}
            GIT_SUBMODULES ""
        )
        FetchContent_Populate(pico_sdk)
        set(PICO_SDK_PATH ${pico_sdk_SOURCE_DIR})
        # Загрузка подмодулей вручную
        find_package(Git QUIET)
        if(GIT_FOUND AND EXISTS "${PICO_SDK_PATH}/.git")
            execute_process(
                COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                WORKING_DIRECTORY ${PICO_SDK_PATH}
                RESULT_VARIABLE GIT_SUBMOD_RESULT
            )
            if(NOT GIT_SUBMOD_RESULT EQUAL "0")
                message(WARNING "Не удалось загрузить подмодули Pico SDK. Попробуйте выполнить вручную: cd ${PICO_SDK_PATH} && git submodule update --init --recursive")
            endif()
        endif()
    else()
        message(FATAL_ERROR "SDK location was not specified. Please set PICO_SDK_PATH or set PICO_SDK_FETCH_FROM_GIT to on")
    endif()
endif()

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

