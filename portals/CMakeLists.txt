include(GNUInstallDirs)

# Static library for the Qt D-Bus clients, services and opt-in configuration.
# It only ever includes the narrow C ABI from src/core_api.h / core_host.h.
add_library(hermes-portals STATIC portals.cpp)
target_compile_options(hermes-portals PRIVATE -Wall -Wextra -Werror)
target_link_libraries(hermes-portals
    PUBLIC Qt6::Core Qt6::DBus
    PRIVATE hermes_core)

add_executable(hermes-portal-backend hermes-portal-backend.cpp)
target_compile_options(hermes-portal-backend PRIVATE -Wall -Wextra -Werror)
target_link_libraries(hermes-portal-backend PRIVATE hermes-portals hermes_core)

add_executable(hermes-portal-selftest ../tools/portal-selftest.cpp)
set_target_properties(hermes-portal-selftest PROPERTIES
    AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(hermes-portal-backend PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_compile_options(hermes-portal-selftest PRIVATE -Wall -Wextra -Werror)
target_link_libraries(hermes-portal-selftest PRIVATE hermes-portals Qt6::Test)

# The service file needs the install prefix, so generate it into the build tree.
configure_file(
    org.freedesktop.impl.portal.desktop.hermes.service
    ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.hermes.service
    @ONLY)

install(TARGETS hermes-portal-backend RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES hermes.portal DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.hermes.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
