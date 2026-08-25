if(CONFIG_ZMK_POINTING)
  target_sources(app PRIVATE src/input_processor_key_guard.c)
endif()
