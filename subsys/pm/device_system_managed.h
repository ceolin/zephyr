/*
 * Copyright (c) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_PM_DEVICE_SYSTEM_MANAGED_H_
#define ZEPHYR_SUBSYS_PM_DEVICE_SYSTEM_MANAGED_H_

#ifdef CONFIG_PM_DEVICE_SYSTEM_MANAGED

bool pm_suspend_devices(const struct pm_state_info *soc_state);
void pm_resume_devices(const struct pm_state_info *soc_state);

#else

bool pm_suspend_devices(const struct pm_state_info *soc_state) { return true; }
void pm_resume_devices(const struct pm_state_info *soc_state) {}

#endif /* CONFIG_PM_DEVICE_SYSTEM_MANAGED */

#endif /* ZEPHYR_SUBSYS_PM_DEVICE_SYSTEM_MANAGED_H_ */
