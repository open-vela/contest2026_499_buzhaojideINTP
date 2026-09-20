#############################################################################
# vendor/allwinner_f1c/boards/f1c100s_board/src/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
#############################################################################

include $(TOPDIR)/Make.defs

CSRCS = f1c100s_appinit.c f1c100s_bringup.c f1c100s_crashlog.c f1c100s_reset.c

ifeq ($(CONFIG_BOARDCTL_BOOT_IMAGE),y)
CSRCS += f1c100s_boot_image.c
endif

ifeq ($(CONFIG_F1C100S_ST7789),y)
CSRCS += f1c100s_st7789.c
endif

ifeq ($(CONFIG_ARCH_LEDS),y)
CSRCS += f1c100s_userleds.c
endif

ifeq ($(CONFIG_ARCH_BUTTONS),y)
CSRCS += f1c100s_buttons.c
endif

ifeq ($(CONFIG_F1C100S_LRADC),y)
CSRCS += f1c100s_adcbuttons.c
endif

RCSRCS = etc/init.d/rc.sysinit etc/init.d/rcS
RCRAWS = etc/group etc/banner.txt etc/switch_guide.txt

include $(TOPDIR)/boards/Board.mk
