
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
    # Headless Assistant tests — no verslicer.app required.
    #   cmake --build build --target assistant_apply_golden_test assistant_pipeline_test
    #   ctest -R 'assistant_(apply_golden|pipeline)_test' --output-on-failure
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
    target_compile_definitions(makerworld_search_test PRIVATE -DBOOST_ALL_NO_LIB -DASSISTANT_HEADLESS_TEST)

    add_executable(model_search_ranker_test
        model_search_ranker_test.cpp
        ../slic3r/GUI/ModelSearch/CandidateRanker.cpp
        ../slic3r/GUI/ModelSearch/ModelSearchDedupe.cpp
        ../slic3r/GUI/MakerWorld/MakerWorldSearchCore.cpp
    )
    target_include_directories(model_search_ranker_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../slic3r
    )
    target_link_libraries(model_search_ranker_test libslic3r boost_headeronly)
    target_compile_definitions(model_search_ranker_test PRIVATE -DBOOST_ALL_NO_LIB -DASSISTANT_HEADLESS_TEST)

    add_executable(assistant_test
        assistant_test.cpp
        ../slic3r/GUI/Assistant/AssistantSettingRegistry.cpp
        ../slic3r/GUI/Assistant/AssistantSettingCatalogBuilder.cpp
        ../slic3r/GUI/Assistant/AssistantSettingSearch.cpp
        ../slic3r/GUI/Assistant/AssistantActionRegistry.cpp
        ../slic3r/GUI/Assistant/AssistantActionJsonExtract.cpp
        ../slic3r/GUI/Assistant/AssistantConfig.cpp
    )
    target_include_directories(assistant_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../slic3r
    )
    target_link_libraries(assistant_test libslic3r boost_headeronly)
    target_compile_definitions(assistant_test PRIVATE -DBOOST_ALL_NO_LIB -DASSISTANT_HEADLESS_TEST)

    add_executable(assistant_pipeline_test
        assistant_pipeline_test.cpp
        ../slic3r/GUI/Assistant/AssistantActionPipelineCore.cpp
        ../slic3r/GUI/Assistant/AssistantActionJsonExtract.cpp
        ../slic3r/GUI/Assistant/AssistantBenchmarkScenarios.cpp
        ../slic3r/GUI/Assistant/AssistantRequestRouter.cpp
        ../slic3r/GUI/Assistant/AssistantAgentGoalPlanner.cpp
        ../slic3r/GUI/Assistant/AssistantSettingSearch.cpp
        ../slic3r/GUI/Assistant/AssistantSettingCatalogBuilder.cpp
        ../slic3r/GUI/Assistant/AssistantSettingRegistry.cpp
        ../slic3r/GUI/Assistant/AssistantConfig.cpp
    )
    target_include_directories(assistant_pipeline_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../slic3r
    )
    target_link_libraries(assistant_pipeline_test libslic3r boost_headeronly)
    target_compile_definitions(assistant_pipeline_test PRIVATE -DBOOST_ALL_NO_LIB -DASSISTANT_HEADLESS_TEST)

    add_executable(mesh_assist_test mesh_assist_test.cpp)
    target_include_directories(mesh_assist_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(mesh_assist_test libslic3r boost_headeronly)
    target_compile_definitions(mesh_assist_test PRIVATE -DBOOST_ALL_NO_LIB)

    add_executable(print_planner_test print_planner_test.cpp)
    target_include_directories(print_planner_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(print_planner_test libslic3r boost_headeronly)
    target_compile_definitions(print_planner_test PRIVATE -DBOOST_ALL_NO_LIB)

    add_executable(assistant_apply_golden_test
        assistant_apply_golden_test.cpp
        ../slic3r/GUI/Assistant/AssistantActionJsonExtract.cpp
        ../slic3r/GUI/Assistant/AssistantConfig.cpp
        ../slic3r/GUI/Assistant/AssistantRequestRouter.cpp
        ../slic3r/GUI/Assistant/AssistantSettingRegistry.cpp
        ../slic3r/GUI/Assistant/AssistantSettingCatalogBuilder.cpp
        ../slic3r/GUI/Assistant/AssistantSettingSearch.cpp
    )
    target_include_directories(assistant_apply_golden_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../slic3r
    )
    target_link_libraries(assistant_apply_golden_test libslic3r boost_headeronly)
    target_compile_definitions(assistant_apply_golden_test PRIVATE -DBOOST_ALL_NO_LIB -DASSISTANT_HEADLESS_TEST)

    add_executable(bambu_wiki_search_test
        bambu_wiki_search_test.cpp
        ../slic3r/GUI/Assistant/BambuLabWikiSearchCore.cpp
        ../slic3r/GUI/Assistant/AssistantActionCritic.cpp
        ../slic3r/GUI/Assistant/AssistantActionJsonExtract.cpp
        ../slic3r/GUI/Assistant/AssistantRequestRouter.cpp
        ../slic3r/GUI/Assistant/AssistantSettingSearch.cpp
        ../slic3r/GUI/Assistant/AssistantSettingCatalogBuilder.cpp
    )
    target_include_directories(bambu_wiki_search_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../slic3r
    )
    target_link_libraries(bambu_wiki_search_test libslic3r boost_headeronly)
    target_compile_definitions(bambu_wiki_search_test PRIVATE -DBOOST_ALL_NO_LIB -DASSISTANT_HEADLESS_TEST)

    add_executable(assistant_benchmark_test
        assistant_benchmark_test.cpp
        ../slic3r/GUI/Assistant/AssistantBenchmarkScenarios.cpp
        ../slic3r/GUI/Assistant/AssistantSettingRegistry.cpp
        ../slic3r/GUI/Assistant/AssistantSettingCatalogBuilder.cpp
        ../slic3r/GUI/Assistant/AssistantRequestRouter.cpp
        ../slic3r/GUI/Assistant/AssistantSettingSearch.cpp
        ../slic3r/GUI/Assistant/AssistantActionJsonExtract.cpp
        ../slic3r/GUI/Assistant/AssistantConfig.cpp
    )
    target_include_directories(assistant_benchmark_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../slic3r
    )
    target_link_libraries(assistant_benchmark_test libslic3r boost_headeronly)
    target_compile_definitions(assistant_benchmark_test PRIVATE -DBOOST_ALL_NO_LIB -DASSISTANT_HEADLESS_TEST)

    add_executable(assistant_eval_test
        assistant_eval_test.cpp
        ../slic3r/GUI/Assistant/AssistantEvalGrader.cpp
        ../slic3r/GUI/Assistant/AssistantActionJsonExtract.cpp
        ../slic3r/GUI/Assistant/AssistantFailureDoctor.cpp
        ../slic3r/GUI/Assistant/AssistantPrinterControl.cpp
        ../slic3r/GUI/Assistant/AssistantSettingRegistry.cpp
        ../slic3r/GUI/Assistant/AssistantSettingCatalogBuilder.cpp
        ../slic3r/GUI/Assistant/AssistantSettingSearch.cpp
        ../slic3r/GUI/Assistant/AssistantConfig.cpp
        ../slic3r/GUI/Assistant/AssistantActionRegistry.cpp
    )
    target_include_directories(assistant_eval_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../slic3r)
    target_link_libraries(assistant_eval_test libslic3r boost_headeronly)
    target_compile_definitions(assistant_eval_test PRIVATE -DBOOST_ALL_NO_LIB -DASSISTANT_HEADLESS_TEST
        -DASSISTANT_EVAL_CASES="${CMAKE_CURRENT_SOURCE_DIR}/eval/assistant_cases.json")

    add_test(NAME assistant_test COMMAND assistant_test)
    add_test(NAME assistant_eval_test COMMAND assistant_eval_test)
    add_test(NAME assistant_pipeline_test COMMAND assistant_pipeline_test)
    add_test(NAME mesh_assist_test COMMAND mesh_assist_test)
    add_test(NAME print_planner_test COMMAND print_planner_test)
    add_test(NAME assistant_benchmark_test COMMAND assistant_benchmark_test)
    add_test(NAME assistant_apply_golden_test COMMAND assistant_apply_golden_test)
    add_test(NAME bambu_wiki_search_test COMMAND bambu_wiki_search_test)
    add_test(NAME makerworld_search_test COMMAND makerworld_search_test)
    add_test(NAME model_search_ranker_test COMMAND model_search_ranker_test)
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
