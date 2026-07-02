
option(SLIC3R_ENC_CHECK "Verify encoding of source files" 1)

if (IS_CROSS_COMPILE)
    # Force disable due to cross compilation. This fact is already printed on cli for users
    set(SLIC3R_ENC_CHECK OFF CACHE BOOL "" FORCE)
endif ()

if (SLIC3R_ENC_CHECK)
    add_executable(encoding-check encoding-check.cpp)

    # A global no-op target which depends on all encodings checks,
    # and on which in turn all checked targets depend.
    # This is done to make encoding checks the first thing to be
    # performed before actually compiling any sources of the checked targets
    # to make the check fail as early as possible.
    add_custom_target(global-encoding-check
        ALL
        DEPENDS encoding-check
    )
endif()

# Function that adds source file encoding check to a target
# using the above encoding-check binary

if (ORCA_TOOLS)
    # Headless Ollama tests — no verslicer.app required.
    #   cmake --build build --target ollama_apply_golden_test ollama_pipeline_test
    #   ctest -R 'ollama_(apply_golden|pipeline)_test' --output-on-failure
    # macOS app bundle (manual QA only): build/<arch>/src/Release/verslicer.app

    add_executable(makerworld_search_test
        makerworld_search_test.cpp
        ../slic3r/GUI/MakerWorld/MakerWorldSearchCore.cpp
        ../slic3r/GUI/MakerWorld/MakerWorldUrl.cpp
    )
    target_include_directories(makerworld_search_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../slic3r
    )
    target_link_libraries(makerworld_search_test libslic3r boost_headeronly)
    target_compile_definitions(makerworld_search_test PRIVATE -DBOOST_ALL_NO_LIB)

    add_executable(ollama_assistant_test
        ollama_assistant_test.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingRegistry.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingCatalogBuilder.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingSearch.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingAliases.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaActionRegistry.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaConfig.cpp
    )
    target_include_directories(ollama_assistant_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../slic3r
    )
    target_link_libraries(ollama_assistant_test libslic3r boost_headeronly)
    target_compile_definitions(ollama_assistant_test PRIVATE -DBOOST_ALL_NO_LIB)

    add_executable(ollama_pipeline_test
        ollama_pipeline_test.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaActionPipelineCore.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaActionJsonExtract.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaBenchmarkScenarios.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaRequestRouter.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaAgentGoalPlanner.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingSearch.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingAliases.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingCatalogBuilder.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingRegistry.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaConfig.cpp
    )
    target_include_directories(ollama_pipeline_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../slic3r
    )
    target_link_libraries(ollama_pipeline_test libslic3r boost_headeronly)
    target_compile_definitions(ollama_pipeline_test PRIVATE -DBOOST_ALL_NO_LIB -DOLLAMA_HEADLESS_TEST)

    add_executable(mesh_assist_test mesh_assist_test.cpp)
    target_include_directories(mesh_assist_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(mesh_assist_test libslic3r boost_headeronly)
    target_compile_definitions(mesh_assist_test PRIVATE -DBOOST_ALL_NO_LIB)

    add_executable(print_planner_test print_planner_test.cpp)
    target_include_directories(print_planner_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(print_planner_test libslic3r boost_headeronly)
    target_compile_definitions(print_planner_test PRIVATE -DBOOST_ALL_NO_LIB)

    add_executable(ollama_apply_golden_test
        ollama_apply_golden_test.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaActionJsonExtract.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaConfig.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaRequestRouter.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingRegistry.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingCatalogBuilder.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingSearch.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingAliases.cpp
    )
    target_include_directories(ollama_apply_golden_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../slic3r
    )
    target_link_libraries(ollama_apply_golden_test libslic3r boost_headeronly)
    target_compile_definitions(ollama_apply_golden_test PRIVATE -DBOOST_ALL_NO_LIB -DOLLAMA_HEADLESS_TEST)

    add_executable(bambu_wiki_search_test
        bambu_wiki_search_test.cpp
        ../slic3r/GUI/OllamaAssistant/BambuLabWikiSearchCore.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaActionCritic.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaRequestRouter.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingSearch.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingAliases.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingCatalogBuilder.cpp
    )
    target_include_directories(bambu_wiki_search_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../slic3r
    )
    target_link_libraries(bambu_wiki_search_test libslic3r boost_headeronly)
    target_compile_definitions(bambu_wiki_search_test PRIVATE -DBOOST_ALL_NO_LIB)

    add_executable(ollama_benchmark_test
        ollama_benchmark_test.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaBenchmarkScenarios.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingRegistry.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingCatalogBuilder.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaRequestRouter.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingSearch.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaSettingAliases.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaActionJsonExtract.cpp
        ../slic3r/GUI/OllamaAssistant/OllamaConfig.cpp
    )
    target_include_directories(ollama_benchmark_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../slic3r
    )
    target_link_libraries(ollama_benchmark_test libslic3r boost_headeronly)
    target_compile_definitions(ollama_benchmark_test PRIVATE -DBOOST_ALL_NO_LIB)

    add_test(NAME ollama_assistant_test COMMAND ollama_assistant_test)
    add_test(NAME ollama_pipeline_test COMMAND ollama_pipeline_test)
    add_test(NAME mesh_assist_test COMMAND mesh_assist_test)
    add_test(NAME print_planner_test COMMAND print_planner_test)
    add_test(NAME ollama_benchmark_test COMMAND ollama_benchmark_test)
    add_test(NAME ollama_apply_golden_test COMMAND ollama_apply_golden_test)
    add_test(NAME bambu_wiki_search_test COMMAND bambu_wiki_search_test)
    add_test(NAME makerworld_search_test COMMAND makerworld_search_test)
endif()

function(encoding_check TARGET)
    if (SLIC3R_ENC_CHECK)
        # Obtain target source files
        get_target_property(T_SOURCES ${TARGET} SOURCES)

        # Define top-level encoding check target for this ${TARGET}
        add_custom_target(encoding-check-${TARGET}
            DEPENDS encoding-check ${T_SOURCES}
            COMMAND $<TARGET_FILE:encoding-check> ${TARGET} ${T_SOURCES}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Checking source files encodings for target ${TARGET}"
        )


        # This adds dependency on encoding-check-${TARGET} to ${TARET}
        # via the global-encoding-check
        add_dependencies(global-encoding-check encoding-check-${TARGET})
        add_dependencies(${TARGET} global-encoding-check)
    endif()
endfunction()
