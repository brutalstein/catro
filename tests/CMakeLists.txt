add_library(catro_test_fixtures STATIC fixtures/capability_fixtures.cpp)
target_include_directories(catro_test_fixtures PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(catro_test_fixtures PUBLIC Catro::Capabilities)
catro_enable_warnings(catro_test_fixtures)

add_custom_target(catro_tests)

# One executable per suite so `ctest -R catro_<suite>` runs a focused set.
function(catro_add_test name source)
    add_executable(catro_${name}_test ${source})
    target_link_libraries(catro_${name}_test PRIVATE catro_test_fixtures Catch2::Catch2WithMain)
    catro_enable_warnings(catro_${name}_test)
    add_dependencies(catro_tests catro_${name}_test)
    add_test(NAME catro_${name} COMMAND catro_${name}_test)
endfunction()

catro_add_test(model capabilities/model_test.cpp)
catro_add_test(validation capabilities/validation_test.cpp)
catro_add_test(snapshot_diff capabilities/snapshot_diff_test.cpp)
catro_add_test(operating_profile capabilities/operating_profile_test.cpp)
catro_add_test(local_envelope capabilities/local_envelope_test.cpp)
catro_add_test(policy capabilities/policy_test.cpp)
catro_add_test(policy_pathology capabilities/policy_pathology_test.cpp)
catro_add_test(probe_coordinator capabilities/probe_coordinator_test.cpp)

catro_add_test(reporting capabilities/reporting_test.cpp)
target_link_libraries(catro_reporting_test PRIVATE Catro::Reporting)
catro_add_test(canonical_determinism capabilities/canonical_determinism_test.cpp)
target_link_libraries(catro_canonical_determinism_test PRIVATE Catro::Reporting)
target_compile_definitions(catro_canonical_determinism_test PRIVATE CATRO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

catro_add_test(community community/community_test.cpp)
target_link_libraries(catro_community_test PRIVATE Catro::Community)

catro_add_test(directory community/directory_test.cpp)
target_link_libraries(catro_directory_test PRIVATE Catro::Community)

catro_add_test(audio audio/audio_test.cpp)
target_link_libraries(catro_audio_test PRIVATE Catro::Audio)

catro_add_test(audio_external audio/external_session_test.cpp)
target_link_libraries(catro_audio_external_test PRIVATE Catro::Audio)

catro_add_test(voice voice/voice_test.cpp)
target_link_libraries(catro_voice_test PRIVATE Catro::Voice)

catro_add_test(voice_jitter voice/jitter_test.cpp)
target_link_libraries(catro_voice_jitter_test PRIVATE Catro::Voice)

catro_add_test(voice_realtime voice/realtime_bridge_test.cpp)
target_link_libraries(catro_voice_realtime_test PRIVATE Catro::Voice)

catro_add_test(voice_pipeline voice/pipeline_test.cpp)
target_link_libraries(catro_voice_pipeline_test PRIVATE Catro::Voice)

catro_add_test(room_voice_runtime voice/room_voice_runtime_test.cpp)
target_link_libraries(catro_room_voice_runtime_test PRIVATE catro_room_voice)

catro_add_test(voice_transport voice/udp_transport_test.cpp)
target_link_libraries(catro_voice_transport_test PRIVATE Catro::Voice catro_voice_peer_transport)

catro_add_test(video_rtp video/rtp_h264_test.cpp)
target_link_libraries(catro_video_rtp_test PRIVATE Catro::Video)

catro_add_test(video_geometry video/geometry_test.cpp)
target_link_libraries(catro_video_geometry_test PRIVATE Catro::Video)

catro_add_test(voice_peer_cli tools/voice_peer_test.cpp)
target_link_libraries(catro_voice_peer_cli_test PRIVATE catro_voice_peer_cli)

catro_add_test(voice_peer_runtime tools/voice_peer_runtime_test.cpp)
target_link_libraries(catro_voice_peer_runtime_test PRIVATE catro_voice_peer_cli)

if((WIN32 AND MSVC) OR APPLE)
    catro_add_test(room_runtime rtc/room_runtime_test.cpp)
    target_link_libraries(catro_room_runtime_test PRIVATE catro_room_runtime)
endif()

if(WIN32 AND MSVC)
    catro_add_test(capture_check tools/capture_check_test.cpp)
    target_link_libraries(catro_capture_check_test PRIVATE catro_capture_check_cli)

    catro_add_test(encode_check tools/encode_check_test.cpp)
    target_link_libraries(catro_encode_check_test PRIVATE catro_encode_check_cli)

    catro_add_test(video_peer_cli tools/video_peer_test.cpp)
    target_link_libraries(catro_video_peer_cli_test PRIVATE catro_video_peer_cli)
endif()

catro_add_test(audio_check tools/audio_check_test.cpp)
target_link_libraries(catro_audio_check_test PRIVATE catro_audio_check_cli)
catro_add_test(diagnostics apps/diagnostics_test.cpp)
target_link_libraries(catro_diagnostics_test PRIVATE catro_diagnostics)
catro_add_test(audio_view_model apps/audio_view_model_test.cpp)
target_link_libraries(catro_audio_view_model_test PRIVATE catro_diagnostics)

catro_add_test(shell_model apps/shell_model_test.cpp)
target_link_libraries(catro_shell_model_test PRIVATE Catro::Shell)

catro_add_test(ui_policy apps/ui_policy_test.cpp)
target_compile_definitions(catro_ui_policy_test PRIVATE
    CATRO_WINDOWS_XAML_DIR="${CMAKE_SOURCE_DIR}/apps/windows/Catro"
)
catro_add_test(report_cli tools/report_cli_test.cpp)
target_link_libraries(catro_report_cli_test PRIVATE catro_report_cli)

# The platform translations are plain C++, so their rules are tested on every platform.
catro_add_test(macos_translation macos/translation_test.cpp)
target_link_libraries(catro_macos_translation_test PRIVATE catro_macos_translation)

catro_add_test(macos_directory_cancellation macos/directory_cancellation_test.cpp)
target_include_directories(catro_macos_directory_cancellation_test PRIVATE
    ${CMAKE_SOURCE_DIR}/platform/macos/include
)
target_link_libraries(catro_macos_directory_cancellation_test PRIVATE Catro::Community)

catro_add_test(windows_translation windows/translation_test.cpp)
target_link_libraries(catro_windows_translation_test PRIVATE catro_windows_translation)

if(APPLE)
    catro_add_test(macos_local_state macos/local_state_test.cpp)
    target_link_libraries(catro_macos_local_state_test PRIVATE Catro::Macos)

    catro_add_test(macos_directory_client macos/directory_client_test.mm)
    target_link_libraries(catro_macos_directory_client_test PRIVATE Catro::Macos "-framework Security")

    catro_add_test(macos_voice_runtime macos/voice_runtime_test.cpp)
    target_link_libraries(catro_macos_voice_runtime_test PRIVATE catro_voice_runtime)
endif()

# Native Windows layers target the MSVC toolchain; other Windows compilers build the portable core only.
if(WIN32 AND MSVC)
    add_executable(catro_hanging_probe_helper helpers/hanging_probe_helper.cpp)
    catro_enable_warnings(catro_hanging_probe_helper)

    catro_add_test(windows_process windows/process_probe_executor_test.cpp)
    target_link_libraries(catro_windows_process_test PRIVATE Catro::Windows)
    target_compile_definitions(catro_windows_process_test PRIVATE
        CATRO_HANGING_PROBE_HELPER="$<TARGET_FILE:catro_hanging_probe_helper>"
    )
    add_dependencies(catro_windows_process_test catro_hanging_probe_helper)

    catro_add_test(windows_local_state windows/local_state_test.cpp)
    target_link_libraries(catro_windows_local_state_test PRIVATE Catro::Windows)

    catro_add_test(windows_voice_runtime windows/voice_runtime_test.cpp)
    target_link_libraries(catro_windows_voice_runtime_test PRIVATE catro_voice_runtime)

    catro_add_test(windows_screen_capture windows/screen_capture_test.cpp)
    target_link_libraries(catro_windows_screen_capture_test PRIVATE Catro::Windows)

    catro_add_test(windows_video_encoder windows/video_encoder_test.cpp)
    target_link_libraries(catro_windows_video_encoder_test PRIVATE Catro::Windows)

    catro_add_test(windows_video_decoder windows/video_decoder_test.cpp)
    target_link_libraries(catro_windows_video_decoder_test PRIVATE Catro::Windows)

    catro_add_test(windows_video_presenter windows/video_presenter_test.cpp)
    target_link_libraries(catro_windows_video_presenter_test PRIVATE Catro::Windows)

    catro_add_test(windows_screen_runtime windows/screen_runtime_test.cpp)
    target_link_libraries(catro_windows_screen_runtime_test PRIVATE Catro::ScreenRuntime)

    catro_add_test(windows_rtc_room windows/rtc_room_transport_test.cpp)
    target_link_libraries(catro_windows_rtc_room_test PRIVATE Catro::Rtc)

    catro_add_test(windows_audio windows/audio_platform_test.cpp)
    target_link_libraries(catro_windows_audio_test PRIVATE Catro::Windows)
    # Catch2 exits with 4 when every case skipped, as on runners without audio endpoints.
    set_tests_properties(catro_windows_audio PROPERTIES SKIP_RETURN_CODE 4)

    catro_add_test(windows_system_runtime windows/system_runtime_probe_test.cpp)
    target_link_libraries(catro_windows_system_runtime_test PRIVATE Catro::Windows Catro::Reporting)
    target_compile_definitions(catro_windows_system_runtime_test PRIVATE
        CATRO_CAPABILITY_PROBE_HELPER="$<TARGET_FILE:catro_capability_probe>"
    )
    add_dependencies(catro_windows_system_runtime_test catro_capability_probe)
endif()
