# ##############################################################################
# packages/demos/contest2026_499_hello_app/CMakeLists.txt
#
# Contest 2026 Team 499 (buzhaojideINTP) - HTTPS Web Service App
# ##############################################################################

if(CONFIG_LVX_USE_DEMO_CONTEST2026_000_HELLO_APP OR CONFIG_EXAMPLES_HTTPSD)
  nuttx_add_application(
    NAME httpsd
    SRCS hello_app_main.c
    STACKSIZE 16384
    PRIORITY 100
    INCLUDE_DIRECTORIES
      ${APPDIR}/crypto/mbedtls/include
      ${APPDIR}/crypto/mbedtls/mbedtls/include
  )

  nuttx_add_application(
    NAME hello_app
    SRCS hello_app.c
    STACKSIZE 16384
    PRIORITY 100
    INCLUDE_DIRECTORIES
      ${APPDIR}/crypto/mbedtls/include
      ${APPDIR}/crypto/mbedtls/mbedtls/include
  )
endif()
