if(CONFIG_SPECTRUM_DONGLE_UART)
  zephyr_library()
  zephyr_library_sources(src/spectrum_dongle_uart.c)
endif()

