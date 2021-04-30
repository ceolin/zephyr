/*
 * Copyright (c) 2018 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <kernel.h>
#include <device.h>
#include <sys/__assert.h>
#include <spinlock.h>

#define LOG_LEVEL CONFIG_PM_LOG_LEVEL /* From power module Kconfig */
#include <logging/log.h>
LOG_MODULE_DECLARE(power);

/* Device PM request type */
#define DEVICE_PM_SYNC   BIT(0)
#define DEVICE_PM_ASYNC  BIT(1)

/*
 * Lets just re-use device pm states for FSM but add these two
 * transitioning states for internal usage.
 */
#define DEVICE_PM_RESUMING_STATE   (DEVICE_PM_OFF_STATE + 1U)
#define DEVICE_PM_SUSPENDING_STATE (DEVICE_PM_RESUMING_STATE + 1U)

static void device_pm_callback(const struct device *dev,
			       int retval, uint32_t *state, void *arg)
{
	__ASSERT(retval == 0, "Device set power state failed");

	atomic_set(&dev->pm->fsm_state, *state);
	if (k_is_pre_kernel()) {
		return;
	}

	/*
	 * This function returns the number of woken threads on success. There
	 * is nothing we can do with this information. Just ignoring it.
	 */
	(void)k_condvar_broadcast(&dev->pm->condvar);
}

static int device_pm_request(const struct device *dev,
			     uint32_t target_state, uint32_t pm_flags)
{
	int ret = 0;
	k_spinlock_key_t key;
	struct k_mutex request_mutex;

	__ASSERT((target_state == DEVICE_PM_ACTIVE_STATE) ||
			(target_state == DEVICE_PM_SUSPEND_STATE),
			"Invalid device PM state requested");

	key = k_spin_lock(&dev->pm->lock);
	if (target_state == DEVICE_PM_ACTIVE_STATE) {
		atomic_inc(&dev->pm->usage);
	} else {
		atomic_dec(&dev->pm->usage);
	}

	switch (dev->pm->fsm_state) {
	case DEVICE_PM_RESUMING_STATE:
		__fallthrough;
	case DEVICE_PM_ACTIVE_STATE:
		if (dev->pm->usage == 0) {
			dev->pm->fsm_state = DEVICE_PM_SUSPENDING_STATE;
			ret = device_set_power_state(dev,
						     DEVICE_PM_SUSPEND_STATE,
						     device_pm_callback, NULL);
		}
		break;
	case DEVICE_PM_SUSPENDING_STATE:
		__fallthrough;
	case DEVICE_PM_SUSPEND_STATE:
		if (dev->pm->usage == 1) {
			dev->pm->fsm_state = DEVICE_PM_RESUMING_STATE;
			ret = device_set_power_state(dev,
						     DEVICE_PM_ACTIVE_STATE,
						     device_pm_callback, NULL);
		}
		break;
	default:
		LOG_ERR("Invalid FSM state!!\n");
		break;
	}

	/*
	 * If the device is active or suspended, there is nothing
	 * else to do.
	 */
	if ((dev->pm->fsm_state == DEVICE_PM_ACTIVE_STATE) ||
		(dev->pm->fsm_state == DEVICE_PM_SUSPEND_STATE)) {
		goto end;
	}

	/*
	 * Return in case of Async request
	 */
	if (pm_flags & DEVICE_PM_ASYNC) {
		k_spin_unlock(&dev->pm->lock, key);
		return 1;
	}

	if (k_is_pre_kernel()) {
		ret = device_set_power_state(dev,
					     DEVICE_PM_ACTIVE_STATE,
					     device_pm_callback, NULL);
		return 0;
	}

	k_mutex_init(&request_mutex);
	k_mutex_lock(&request_mutex, K_FOREVER);
	(void)k_condvar_wait(&dev->pm->condvar, &request_mutex, K_FOREVER);
	k_mutex_unlock(&request_mutex);

end:
	k_spin_unlock(&dev->pm->lock, key);
	return target_state == atomic_get(&dev->pm->fsm_state) ? 0 : -EIO;
}

int device_pm_get(const struct device *dev)
{
	return device_pm_request(dev,
			DEVICE_PM_ACTIVE_STATE, DEVICE_PM_ASYNC);
}

int device_pm_get_sync(const struct device *dev)
{
	return device_pm_request(dev, DEVICE_PM_ACTIVE_STATE, 0);
}

int device_pm_put(const struct device *dev)
{
	return device_pm_request(dev,
			DEVICE_PM_SUSPEND_STATE, DEVICE_PM_ASYNC);
}

int device_pm_put_sync(const struct device *dev)
{
	return device_pm_request(dev, DEVICE_PM_SUSPEND_STATE, 0);
}

void device_pm_enable(const struct device *dev)
{
	int ret;
	k_spinlock_key_t key;

	key = k_spin_lock(&dev->pm->lock);
	if (!dev->pm->enable) {
		dev->pm->enable = true;
	}

	ret = device_set_power_state(dev, DEVICE_PM_ACTIVE_STATE,
				     device_pm_callback, NULL);
	k_spin_unlock(&dev->pm->lock, key);
	__ASSERT_NO_MSG(ret == 0);
}

void device_pm_disable(const struct device *dev)
{
	k_spinlock_key_t key;

	__ASSERT(k_is_pre_kernel() == false, "Device should not be disabled "
		 "before kernel is initialized");

	key = k_spin_lock(&dev->pm->lock);
	dev->pm->enable = false;
	k_spin_unlock(&dev->pm->lock, key);
}
