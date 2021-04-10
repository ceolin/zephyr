/*
 * Copyright (c) 2018 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <kernel.h>
#include <string.h>
#include <device.h>
#include <sys/types.h>

#include "policy/pm_policy.h"

#if defined(CONFIG_PM)
#define LOG_LEVEL CONFIG_PM_LOG_LEVEL /* From power module Kconfig */
#include <logging/log.h>
LOG_MODULE_DECLARE(power);

/*
 * This weak symbol will be override by gen-handles.py
 *
 * This return a list of devices sorted by dependencies between devices
 * ensuring that all dependencies of a device precede its.
 */
__weak struct device * const *z_get_pm_devices(size_t *len)
{
	*len = 0;
	return NULL;
}

static int _pm_devices(uint32_t state)
{
	ssize_t i;
	size_t len;
	struct device * const *pm_devices = z_get_pm_devices(&len);

	for (i = len - 1; i >= 0; i--) {
		int rc;
		struct device *dev = pm_devices[i];

		rc = device_set_power_state(dev, state, NULL, NULL);
		if ((rc != -ENOTSUP) && (rc != 0)) {
			LOG_DBG("%s did not enter %s state: %d",
				dev->name, device_pm_state_str(state), rc);
			return rc;
		}
	}

	return 0;
}

int pm_suspend_devices(void)
{
	return _pm_devices(DEVICE_PM_SUSPEND_STATE);
}

int pm_low_power_devices(void)
{
	return _pm_devices(DEVICE_PM_LOW_POWER_STATE);
}

int pm_force_suspend_devices(void)
{
	return _pm_devices(DEVICE_PM_FORCE_SUSPEND_STATE);
}

void pm_resume_devices(void)
{
	size_t i, len;
	struct device * const *pm_devices = z_get_pm_devices(&len);

	for (i = 0; i < len; i++) {
		struct device *dev = pm_devices[i];

		device_set_power_state(dev,
				       DEVICE_PM_ACTIVE_STATE,
				       NULL, NULL);
	}
}

#endif /* defined(CONFIG_PM) */

const char *device_pm_state_str(uint32_t state)
{
	switch (state) {
	case DEVICE_PM_ACTIVE_STATE:
		return "active";
	case DEVICE_PM_LOW_POWER_STATE:
		return "low power";
	case DEVICE_PM_SUSPEND_STATE:
		return "suspend";
	case DEVICE_PM_FORCE_SUSPEND_STATE:
		return "force suspend";
	case DEVICE_PM_OFF_STATE:
		return "off";
	default:
		return "";
	}
}
