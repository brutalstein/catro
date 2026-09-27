# SwiftUI diagnostics shell. CMake generates the Xcode project (macos-clang preset), so the shell
# links the core, platform, and view-model targets directly instead of prebuilt libraries.
enable_language(Swift)

set(bridge_header ${CMAKE_CURRENT_SOURCE_DIR}/Catro/Bridge/CatroCapabilitiesBridge.h)

add_executable(catro_macos_app MACOSX_BUNDLE
    Catro/CatroApp.swift
    Catro/Diagnostics/DiagnosticsView.swift
    Catro/Diagnostics/DiagnosticsViewModel.swift
    Catro/Audio/AudioView.swift
    Catro/Audio/AudioViewModel.swift
    Catro/Bridge/CatroCapabilitiesBridge.mm
    Catro/Bridge/CatroAudioBridge.mm
    ${bridge_header})
set_target_properties(catro_macos_app PROPERTIES
    OUTPUT_NAME "Catro"
    Swift_LANGUAGE_VERSION 5
    # Adds NSMicrophoneUsageDescription; macOS refuses microphone access without it.
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "Catro"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.catro.Catro"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "dev.catro.Catro")
target_include_directories(catro_macos_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Catro/Bridge)
target_compile_options(catro_macos_app PRIVATE
    "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-import-objc-header ${bridge_header}>"
    "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_link_libraries(catro_macos_app PRIVATE catro_diagnostics Catro::Macos
    "-framework AppKit" "-framework SwiftUI")

# The passive probe helper runs from beside the application executable.
add_dependencies(catro_macos_app catro_capability_probe)
add_custom_command(TARGET catro_macos_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:catro_capability_probe> $<TARGET_FILE_DIR:catro_macos_app>
    VERBATIM)
