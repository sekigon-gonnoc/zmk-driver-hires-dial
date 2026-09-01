zephyr_library()

zephyr_library_include_directories(include)
zephyr_library_sources_ifdef(CONFIG_ZMK_HIRES_DIAL src/hires_dial.c)
zephyr_library_sources(src/remote_hires_dial_sensor.c)

if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources_ifdef(CONFIG_ZMK_HIRES_DIAL src/behaviors/behavior_hires_dial_encoder.c)
  # Keep referenced behavior devices available when an optional output is disabled. The
  # implementations consume input without emitting events in that configuration.
  zephyr_library_sources_ifdef(CONFIG_ZMK_HIRES_DIAL src/behaviors/behavior_hires_dial_scroll.c)
  zephyr_library_sources_ifdef(CONFIG_ZMK_HIRES_DIAL src/behaviors/behavior_hires_dial_radial_controller.c)
  zephyr_library_sources_ifdef(CONFIG_ZMK_HIRES_DIAL src/behaviors/behavior_hires_dial_radial_controller_button.c)
  zephyr_library_sources_ifdef(CONFIG_ZMK_HIRES_DIAL_RADIAL_CONTROLLER src/radial_controller/hid.c)
  zephyr_library_sources_ifdef(CONFIG_ZMK_HIRES_DIAL_RADIAL_CONTROLLER src/radial_controller/endpoints.c)

  if(CONFIG_ZMK_HIRES_DIAL_RADIAL_CONTROLLER AND CONFIG_ZMK_USB)
    zephyr_library_sources(src/radial_controller/usb_hid.c)
  endif()

  if(CONFIG_ZMK_HIRES_DIAL_RADIAL_CONTROLLER AND CONFIG_ZMK_BLE)
    zephyr_library_sources(src/radial_controller/hog.c)
  endif()
endif()
