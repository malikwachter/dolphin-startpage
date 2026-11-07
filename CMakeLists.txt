cmake_minimum_required(VERSION 3.16)
project(dolphin-startpage VERSION 0.2.1 LANGUAGES CXX)

option(BUILD_WITH_QT6 "Build against Qt6/KF6 (Plasma 6)" ON)
if(BUILD_WITH_QT6)
    set(QT_MAJOR_VERSION 6)
    set(KF_MIN 6.0.0)
else()
    set(QT_MAJOR_VERSION 5)
    set(KF_MIN 5.98.0)
endif()
set(KFV KF${QT_MAJOR_VERSION})
set(QTV Qt${QT_MAJOR_VERSION})

find_package(ECM ${KF_MIN} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(${QTV} REQUIRED COMPONENTS Core Widgets)
find_package(${KFV} ${KF_MIN} REQUIRED COMPONENTS KIO WidgetsAddons Config CoreAddons Solid)

# In-process part: loaded by Dolphin through the KOverlayIconPlugin mechanism,
# then attaches the start page UI to every DolphinView showing startpage:/
add_library(dolphinstartpage MODULE
    src/startpageplugin.cpp
    src/startpagewidget.cpp
    src/flowlayout.cpp)
target_link_libraries(dolphinstartpage
    ${QTV}::Widgets
    ${KFV}::KIOCore ${KFV}::KIOWidgets ${KFV}::KIOFileWidgets
    ${KFV}::WidgetsAddons ${KFV}::ConfigCore ${KFV}::CoreAddons ${KFV}::Solid)
install(TARGETS dolphinstartpage DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf${QT_MAJOR_VERSION}/overlayicon)

# KIO worker providing the startpage:/ URL (also a usable fallback listing)
add_library(kio_startpage MODULE src/kio_startpage.cpp)
set_target_properties(kio_startpage PROPERTIES PREFIX "")
target_link_libraries(kio_startpage ${QTV}::Core ${KFV}::KIOCore)
install(TARGETS kio_startpage DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf${QT_MAJOR_VERSION}/kio)
