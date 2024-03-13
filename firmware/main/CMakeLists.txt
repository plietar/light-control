set(srcs main.c indicator.c light.c local_control.c button.c version.c config.c)
set(requires json nvs_flash esp_app_format esp_wifi bt)

if(CONFIG_RUUVI_ENABLE)
  list(APPEND srcs ble.c ruuvi.c)
endif()


idf_component_register(SRCS ${srcs} REQUIRES ${requires} INCLUDE_DIRS ".")

file(STRINGS "${CMAKE_SOURCE_DIR}/date.txt" PROJECT_BUILD_DATE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/date.txt")

set_source_files_properties(
  SOURCE "version.c"
  PROPERTIES COMPILE_DEFINITIONS
  "PROJECT_BUILD_DATE=\"${PROJECT_BUILD_DATE}\"")
