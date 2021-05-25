/*
 * Copyright (c) 2018 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <kernel.h>
#include <string.h>
#include <device.h>
#include "policy/pm_policy.h"

#if defined(CONFIG_PM)
#define LOG_LEVEL CONFIG_PM_LOG_LEVEL /* From power module Kconfig */
#include <logging/log.h>
LOG_MODULE_DECLARE(power);

/* Ordinal of sufficient size to index available devices. */
typedef uint16_t device_idx_t;

/* The maximum value representable with a device_idx_t. */
#define DEVICE_IDX_MAX ((device_idx_t)(-1))

/* An array of all devices in the application. */
static const struct device *all_devices;

/* Indexes into all_devices for devices that support pm,
 * in dependency order (later may depend on earlier).
 */
static const struct device *pm_devices[CONFIG_PM_MAX_DEVICES];

/* Number of devices that support pm */
static device_idx_t num_pm;

/* Number of devices successfully suspended. */
static device_idx_t num_susp;

static bool should_suspend(const struct device *dev, uint32_t state)
{
	int rc;
	uint32_t current_state;

	if (device_busy_check(dev) != 0) {
		return false;
	}

	rc = pm_device_state_get(dev, &current_state);
	if ((rc != -ENOSYS) && (rc != 0)) {
		LOG_DBG("Was not possible to get device %s state: %d",
			dev->name, rc);
		return true;
	}

	/*
	 * If the device is currently powered off or the request was
	 * to go to the same state, just ignore it.
	 */
	if ((current_state == PM_DEVICE_STATE_OFF) ||
			(current_state == state)) {
		return false;
	}

	return true;
}

static int _pm_devices(uint32_t state)
{
	num_susp = 0;

	for (int i = num_pm - 1; i >= 0; i--) {
		const struct device *dev = pm_devices[i];
		bool suspend;
		int rc;

		suspend = should_suspend(dev, state);
		if (suspend) {
			/*
			 * Don't bother the device if it is currently
			 * in the right state.
			 */
			rc = pm_device_state_set(dev, state, NULL, NULL);
			if ((rc != -ENOSYS) && (rc != 0)) {
				LOG_DBG("%s did not enter %s state: %d",
					dev->name, pm_device_state_str(state),
					rc);
				return rc;
			}

			/*
			 * Just mark as suspended devices that were suspended now
			 * otherwise we will resume devices that were already suspended
			 * and not being used.
			 * This still not optimal, since we are not distinguishing
			 * between other states like DEVICE_PM_LOW_POWER_STATE.
			 */
			++num_susp;
		}
	}

	return 0;
}

int pm_suspend_devices(void)
{
	return _pm_devices(PM_DEVICE_STATE_SUSPEND);
}

int pm_low_power_devices(void)
{
	return _pm_devices(PM_DEVICE_STATE_LOW_POWER);
}

int pm_force_suspend_devices(void)
{
	return _pm_devices(PM_DEVICE_STATE_FORCE_SUSPEND);
}

void pm_resume_devices(void)
{
	device_idx_t pmi = num_pm - num_susp;

	num_susp = 0;
	while (pmi < num_pm) {
		pm_device_state_set(pm_devices[pmi],
				       PM_DEVICE_STATE_ACTIVE,
				       NULL, NULL);
		++pmi;
	}
}

static bool check_device_in_list(const struct device *dev)
{
	device_idx_t i;

	for (i = 0; i < num_pm; i++) {
		if (pm_devices[i] == dev)
			return true;
	}

	return false;
}

static int device_foreach_cb(const struct device *dev,
			     void *ctx)
{
	(void) device_required_foreach(dev, device_foreach_cb, ctx);

	if (dev->pm_control == NULL) {
		return 0;
	}

	if (check_device_in_list(dev)) {
		return 0;
	}

	__ASSERT(num_pm <= CONFIG_PM_MAX_DEVICES,
		"Number of pm devices is greater than CONFIG_PM_MAX_DEVICES");
	pm_devices[num_pm] = dev;
	num_pm++;

	return 0;
}

void pm_create_device_list(void)
{
	size_t count = z_device_get_all_static(&all_devices);
	device_idx_t pmi;

	/*
	 * Create an ordered list of devices that will be suspended.
	 * Ordering should be done based on dependencies. Devices
	 * in the beginning of the list will be resumed first.
	 */

	__ASSERT_NO_MSG(count <= DEVICE_IDX_MAX);

	num_pm = 0;
	for (pmi = 0; pmi < count; pmi++) {
		const struct device *dev = &all_devices[pmi];

		/* Ignore "device"s that don't support PM */
		if (dev->pm_control == NULL) {
			continue;
		}

		(void) device_required_foreach(dev, device_foreach_cb, &num_pm);
		if (!check_device_in_list(dev)) {
			pm_devices[num_pm] = dev;
			num_pm++;
		}
	}
}
#endif /* defined(CONFIG_PM) */

const char *pm_device_state_str(uint32_t state)
{
	switch (state) {
	case PM_DEVICE_STATE_ACTIVE:
		return "active";
	case PM_DEVICE_STATE_LOW_POWER:
		return "low power";
	case PM_DEVICE_STATE_SUSPEND:
		return "suspend";
	case PM_DEVICE_STATE_FORCE_SUSPEND:
		return "force suspend";
	case PM_DEVICE_STATE_OFF:
		return "off";
	default:
		return "";
	}
}

int pm_device_state_set(const struct device *dev, uint32_t device_power_state,
			pm_device_cb cb, void *arg)
{
	if (dev->pm_control == NULL) {
		return -ENOSYS;
	}

	return dev->pm_control(dev, PM_DEVICE_STATE_SET,
			       &device_power_state, cb, arg);
}

int pm_device_state_get(const struct device *dev, uint32_t *device_power_state)
{
	if (dev->pm_control == NULL) {
		return -ENOSYS;
	}

	return dev->pm_control(dev, PM_DEVICE_STATE_GET,
			       device_power_state, NULL, NULL);
}
