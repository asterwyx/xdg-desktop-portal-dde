find_package(PkgConfig REQUIRED)
pkg_get_variable(WlrProtocols_PKGDATADIR wlr-protocols pkgdatadir)
pkg_get_variable(WaylandProtocols_PKGDATADIR wayland-protocols pkgdatadir)
pkg_get_variable(TreelandProtocols_PKGDATADIR treeland-protocols pkgdatadir)
find_package(Qt6 COMPONENTS REQUIRED Core DBus WaylandClient WaylandScannerTools)
pkg_check_modules(Pipewire IMPORTED_TARGET REQUIRED libpipewire-0.3 libspa-0.2)
pkg_check_modules(GBM IMPORTED_TARGET REQUIRED gbm)
pkg_check_modules(Wayland IMPORTED_TARGET REQUIRED wayland-client)
qt_add_dbus_adaptor(SCREENCAST_ADAPTORS ${XdpInterfacesDir}/org.freedesktop.impl.portal.ScreenCast.xml portalwaylandcontext.h PortalWaylandContext)
add_library(xdg-desktop-portal-dde-wayland STATIC
        ${SCREENCAST_ADAPTORS}
        portalwaylandcontext.h
        portalwaylandcontext.cpp
        screenshotportal.h
        screenshotportal.cpp
        abstractwaylandportal.h
        screenshotframe.h
        screenshotframe.cpp
        common.h
        treelandcapture.h
        treelandcapture.cpp
        screencast.h
        screencast.cpp
        screencastframe.h
        screencastframe.cpp
        fpslimit.h
        fpslimit.cpp
        timespecutil.h
        timespecutil.cpp
        screencopymanager.h
        screencastchooserdialog.cpp
        screencastchooserdialog.h
        screencasttargetmodel.cpp
        screencasttargetmodel.h
)

qt_generate_wayland_protocol_client_sources(xdg-desktop-portal-dde-wayland NO_INCLUDE_CORE_ONLY FILES
        ${WlrProtocols_PKGDATADIR}/unstable/wlr-screencopy-unstable-v1.xml
        ${TreelandProtocols_PKGDATADIR}/treeland-capture-unstable-v1.xml
        ${WaylandProtocols_PKGDATADIR}/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml
)

target_include_directories(xdg-desktop-portal-dde-wayland
        PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_options(xdg-desktop-portal-dde-wayland PRIVATE -Wno-address-of-temporary)

target_link_libraries(xdg-desktop-portal-dde-wayland
        PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::DBus
        Qt6::GuiPrivate
        Qt6::WaylandClientPrivate
        PkgConfig::Pipewire
        PkgConfig::GBM
        PkgConfig::Wayland
)
