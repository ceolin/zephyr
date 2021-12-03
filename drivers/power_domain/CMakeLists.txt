# Copyright (c) 2022, CSIRO
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_PM_DEVICE_POWER_DOMAIN_DYNAMIC power_domain.c)
zephyr_library_sources_ifdef(CONFIG_POWER_DOMAIN_GPIO power_domain_gpio.c)
