cmake_minimum_required(VERSION 3.20)
project(Grounded2PanelES VERSION 0.3.1 LANGUAGES C CXX RC)

# Grounded 2 · Panel en español
#   Grounded2PanelES.dll  -> panel que se carga dentro del juego
#   IniciarPanel.exe      -> iniciador (abre el juego, carga el panel, copias de seguridad)
#   VistaPrevia.exe       -> interfaz con datos de ejemplo, para revisar el diseño sin el juego
#
# Requiere el SDK de Dumper-7 ya parcheado con scripts/patch_sdk.py (build.sh lo prepara).

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(G2_SDK_DIR "${CMAKE_SOURCE_DIR}/build/sdk" CACHE PATH "SDK de Dumper-7 parcheado (contiene SDK.hpp y SDK/)")
if(NOT EXISTS "${G2_SDK_DIR}/SDK/Basic.hpp")
    message(FATAL_ERROR "No se encontró el SDK en ${G2_SDK_DIR}. Ejecuta build.sh o indica -DG2_SDK_DIR=...")
endif()

set(COMMON_DEFS UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX TARGET_PLATFORM=1
    G2P_VERSION="${PROJECT_VERSION}")
set(STATIC_RUNTIME -static -static-libgcc -static-libstdc++)

# Resource scripts (icon, embedded fonts, version information)
set(G2_ASSETS "${CMAKE_SOURCE_DIR}/assets")
set(G2_SOURCE "${CMAKE_SOURCE_DIR}")
set(G2_NAME "Grounded2PanelES.dll")
set(G2_DESCRIPTION "Grounded 2 - Panel en espanol")
set(G2_FILETYPE "VFT_DLL")
configure_file(src/panel/Panel.rc.in "${CMAKE_BINARY_DIR}/Panel.rc" @ONLY)
set(G2_NAME "IniciarPanel.exe")
set(G2_DESCRIPTION "Grounded 2 - Iniciar el panel en espanol")
set(G2_FILETYPE "VFT_APP")
configure_file(src/launcher/Launcher.rc.in "${CMAKE_BINARY_DIR}/Launcher.rc" @ONLY)
set(G2_NAME "VistaPrevia.exe")
set(G2_DESCRIPTION "Grounded 2 - Vista previa del panel")
configure_file(src/launcher/Launcher.rc.in "${CMAKE_BINARY_DIR}/Preview.rc" @ONLY)

# ---------------------------------------------------------------- SDK (generated code)
set(SDK_SOURCES
    Basic.cpp
    CoreUObject_functions.cpp
    Engine_functions.cpp
    Maine_functions.cpp
    MaineCommon_functions.cpp
    OEIText_functions.cpp
    BP_SurvivalGameMode_functions.cpp
)
list(TRANSFORM SDK_SOURCES PREPEND "${G2_SDK_DIR}/SDK/")
add_library(g2sdk STATIC ${SDK_SOURCES})
target_include_directories(g2sdk SYSTEM PUBLIC "${G2_SDK_DIR}" "${CMAKE_SOURCE_DIR}/cmake/compat")
target_compile_definitions(g2sdk PUBLIC ${COMMON_DEFS})
# Layout static_asserts in Assertions.inl stay active; only warnings are silenced.
target_compile_options(g2sdk PRIVATE -w -O1 -fno-strict-aliasing -ffunction-sections -fdata-sections)

# ---------------------------------------------------------------- Dear ImGui
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC UNICODE _UNICODE IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_compile_options(imgui PRIVATE -O2 -w)

# ---------------------------------------------------------------- shared UI (theme + window host)
add_library(g2ui STATIC
    src/common/Theme.cpp
    src/common/DxWindow.cpp)
target_include_directories(g2ui PUBLIC src)
target_compile_definitions(g2ui PUBLIC ${COMMON_DEFS})
target_link_libraries(g2ui PUBLIC imgui d3d11 dxgi d3dcompiler dwmapi uxtheme)
target_compile_options(g2ui PRIVATE -O2 -Wall -Wextra)

# ---------------------------------------------------------------- panel pages (no SDK: talks through Bridge)
add_library(g2pages STATIC
    src/panel/ui/Pages.cpp
    src/panel/ui/PanelWindow.cpp
    src/panel/game/Settings.cpp
    src/panel/core/Log.cpp)
target_include_directories(g2pages PUBLIC src src/panel)
target_link_libraries(g2pages PUBLIC g2ui)
target_compile_options(g2pages PRIVATE -O2 -Wall -Wextra)

# ---------------------------------------------------------------- panel DLL
add_library(Grounded2PanelES SHARED
    src/panel/dllmain.cpp
    src/panel/core/Memory.cpp
    src/panel/core/Resolver.cpp
    src/panel/core/Reflect.cpp
    src/panel/game/Hooks.cpp
    src/panel/game/Session.cpp
    src/panel/game/Catalog.cpp
    src/panel/game/Actions.cpp
    src/panel/game/Features.cpp
    "${CMAKE_BINARY_DIR}/Panel.rc")
set_target_properties(Grounded2PanelES PROPERTIES PREFIX "" OUTPUT_NAME "Grounded2PanelES")
target_include_directories(Grounded2PanelES PRIVATE src src/panel)
target_link_libraries(Grounded2PanelES PRIVATE g2pages g2sdk ole32 shell32 version)
target_compile_options(Grounded2PanelES PRIVATE -O2 -Wall -Wextra -Wno-invalid-offsetof -fno-strict-aliasing)
target_link_options(Grounded2PanelES PRIVATE ${STATIC_RUNTIME} -Wl,--gc-sections -Wl,--exclude-all-symbols -s)

# ---------------------------------------------------------------- launcher
add_executable(IniciarPanel WIN32
    src/launcher/Launcher.cpp
    src/launcher/Steam.cpp
    src/launcher/Inject.cpp
    src/launcher/Backup.cpp
    "${CMAKE_BINARY_DIR}/Launcher.rc")
target_include_directories(IniciarPanel PRIVATE src)
target_link_libraries(IniciarPanel PRIVATE g2ui ole32 shell32 shlwapi version advapi32)
target_compile_options(IniciarPanel PRIVATE -O2 -Wall -Wextra)
target_link_options(IniciarPanel PRIVATE ${STATIC_RUNTIME} -municode -s)

# ---------------------------------------------------------------- preview (design review without the game)
add_executable(VistaPrevia WIN32 tools/Preview.cpp "${CMAKE_BINARY_DIR}/Preview.rc")
target_include_directories(VistaPrevia PRIVATE src src/panel)
target_link_libraries(VistaPrevia PRIVATE g2pages ole32 shell32)
target_compile_options(VistaPrevia PRIVATE -O2 -Wall -Wextra)
target_link_options(VistaPrevia PRIVATE ${STATIC_RUNTIME} -municode -s)

# ---------------------------------------------------------------- integration test (not shipped)
# PruebaMotor.exe simulates Unreal's globals at unknown addresses and checks that the panel finds them.
add_executable(PruebaMotor tests/FakeEngine.cpp)
target_link_libraries(PruebaMotor PRIVATE ole32 shell32 uuid)
target_link_options(PruebaMotor PRIVATE ${STATIC_RUNTIME} -municode)
