# ##############################################################################
# vendor/allwinner/chips/f1c100s/CMakeLists.txt
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

# Core system files (always compiled)
set(SRCS
    f1c100s_head.S
    f1c100s_start.c
    f1c100s_irq.c
    f1c100s_allocateheap.c
    f1c100s_clock.c
    f1c100s_mmu.c
    f1c100s_dram.c
    f1c100s_gpio.c
    f1c100s_boot.c
    f1c100s_softreset.c
    f1c100s_vectors.c
    f1c100s_idle.c
    f1c100s_oneshot.c)

# UART driver
if(CONFIG_F1C100S_UART0 OR CONFIG_F1C100S_UART1 OR CONFIG_F1C100S_UART2)
  list(APPEND SRCS f1c100s_uart.c)
endif()

# SPI driver
if(CONFIG_F1C100S_SPI0 OR CONFIG_F1C100S_SPI1)
  list(APPEND SRCS f1c100s_spi.c)
endif()

# I2C driver
if(CONFIG_F1C100S_I2C0 OR CONFIG_F1C100S_I2C1 OR CONFIG_F1C100S_I2C2)
  list(APPEND SRCS f1c100s_i2c.c)
endif()

# SDIO driver
if(CONFIG_F1C100S_SDIO)
  list(APPEND SRCS f1c100s_sdio.c)
endif()

# Audio Codec driver
if(CONFIG_F1C100S_AUDIO)
  list(APPEND SRCS f1c100s_audio.c)
endif()

# DMA driver
if(CONFIG_F1C100S_DMA)
  list(APPEND SRCS f1c100s_dma.c)
endif()

# Watchdog Timer driver
if(CONFIG_F1C100S_WDT)
  list(APPEND SRCS f1c100s_wdt.c f1c100s_wdtfeed.c)
endif()

# Framebuffer driver
if(CONFIG_F1C100S_FB)
  list(APPEND SRCS f1c100s_fb.c)
endif()

# Boot KV (dual-slot NOR state)
if(CONFIG_F1C100S_BOOTKV)
  list(APPEND SRCS f1c100s_bootkv.c f1c100s_bootctl.c)
endif()

target_sources(arch PRIVATE ${SRCS})

target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/include)

# Workaround: Remove arm_head.S from arch target since we provide f1c100s_head.S
# The arm/CMakeLists.txt unconditionally adds arm_head.S which conflicts with our
# custom head file. This workaround avoids modifying NuttX upstream code.
get_target_property(ARCH_SOURCES arch SOURCES)
list(FILTER ARCH_SOURCES EXCLUDE REGEX "arm/arm_head\\.S$")
set_target_properties(arch PROPERTIES SOURCES "${ARCH_SOURCES}")
