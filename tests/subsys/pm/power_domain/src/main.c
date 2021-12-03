/*
 * Copyright (c) 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ztest.h>
#include <pm/device.h>
#include <pm/device_runtime.h>
#include <drivers/power_domain.h>

static int testing_domain_on_notitication;
static int testing_domain_off_notitication;

#define TEST_DOMAIN DT_NODELABEL(test_domain)
#define TEST_DEVA DT_NODELABEL(test_dev_a)
#define TEST_DEVB DT_NODELABEL(test_dev_b)

static const struct device *domain, *deva, *devb, *devc;

static int dev_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static int pm_action_domain(const struct device *dev,
	enum pm_device_action action)
{
	int rc = 0;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		/* Switch power on */
		pm_device_children_action_run(dev, PM_DEVICE_ACTION_TURN_ON, NULL);
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		pm_device_children_action_run(dev, PM_DEVICE_ACTION_TURN_OFF, NULL);
		break;
	case PM_DEVICE_ACTION_TURN_ON:
		__fallthrough;
	case PM_DEVICE_ACTION_TURN_OFF:
		break;
	default:
		rc = -ENOTSUP;
	}

	return rc;

}

static int pm_action(const struct device *dev,
		     enum pm_device_action pm_action)
{
	ARG_UNUSED(dev);

	if (testing_domain_on_notitication > 0) {
		if (pm_action == PM_DEVICE_ACTION_TURN_ON) {
			testing_domain_on_notitication--;
		}
	} else if (testing_domain_off_notitication > 0) {
		if (pm_action == PM_DEVICE_ACTION_TURN_OFF) {
			testing_domain_off_notitication--;
		}
	}

	return 0;
}

PM_DEVICE_DT_DEFINE(TEST_DOMAIN, pm_action_domain);
DEVICE_DT_DEFINE(TEST_DOMAIN, dev_init, PM_DEVICE_DT_REF(TEST_DOMAIN),
		 NULL, NULL, POST_KERNEL, 10, NULL);

PM_DEVICE_DT_DEFINE(TEST_DEVA, pm_action);
DEVICE_DT_DEFINE(TEST_DEVA, dev_init, PM_DEVICE_DT_REF(TEST_DEVA),
		 NULL, NULL, POST_KERNEL, 20, NULL);

PM_DEVICE_DT_DEFINE(TEST_DEVB, pm_action);
DEVICE_DT_DEFINE(TEST_DEVB, dev_init, PM_DEVICE_DT_REF(TEST_DEVB),
		 NULL, NULL, POST_KERNEL, 30, NULL);

PM_DEVICE_DEFINE(devc, pm_action);
DEVICE_DEFINE(devc, "devc", dev_init, PM_DEVICE_REF(devc),
	      NULL, NULL,
	      APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

/**
 * @brief Test the power domain behavior
 *
 * Scenarios tested:
 *
 * - get + put multiple devices under a domain
 * - notification when domain state changes
 */
static void test_power_domain_device_runtime(void)
{
	int ret;
	enum pm_device_state state;

	domain = DEVICE_DT_GET(TEST_DOMAIN);
	zassert_not_null(domain, "Failed to get device");

	deva = DEVICE_DT_GET(TEST_DEVA);
	zassert_not_null(deva, "Failed to get device");

	devb = DEVICE_DT_GET(TEST_DEVB);
	zassert_not_null(devb, "Failed to get device");

	devc = DEVICE_GET(devc);

	pm_device_runtime_init_suspended(domain);
	pm_device_runtime_init_suspended(deva);
	pm_device_runtime_init_suspended(devb);
	pm_device_runtime_init_suspended(devc);

	pm_device_runtime_enable(domain);
	pm_device_runtime_enable(deva);
	pm_device_runtime_enable(devb);
	pm_device_runtime_enable(devc);

	pm_device_power_domain_add(devc, domain);

	/* At this point all devices should be SUSPENDED */
	pm_device_state_get(domain, &state);
	zassert_equal(state, PM_DEVICE_STATE_SUSPENDED, NULL);

	pm_device_state_get(deva, &state);
	zassert_equal(state, PM_DEVICE_STATE_SUSPENDED, NULL);

	pm_device_state_get(devb, &state);
	zassert_equal(state, PM_DEVICE_STATE_SUSPENDED, NULL);

	pm_device_state_get(devc, &state);
	zassert_equal(state, PM_DEVICE_STATE_SUSPENDED, NULL);

	/* Now test if "get" a device will resume the domain */
	ret = pm_device_runtime_get(deva);
	zassert_equal(ret, 0, NULL);

	pm_device_state_get(deva, &state);
	zassert_equal(state, PM_DEVICE_STATE_ACTIVE, NULL);

	pm_device_state_get(domain, &state);
	zassert_equal(state, PM_DEVICE_STATE_ACTIVE, NULL);

	ret = pm_device_runtime_get(devc);
	zassert_equal(ret, 0, NULL);

	ret = pm_device_runtime_get(devb);
	zassert_equal(ret, 0, NULL);

	ret = pm_device_runtime_put(deva);
	zassert_equal(ret, 0, NULL);

	/*
	 * The domain has to still be active since device B and C
	 * are still in use.
	 */
	pm_device_state_get(domain, &state);
	zassert_equal(state, PM_DEVICE_STATE_ACTIVE, NULL);

	/*
	 * Now the domain should be suspended since there is no
	 * one using it.
	 */
	ret = pm_device_runtime_put(devb);
	zassert_equal(ret, 0, NULL);

	ret = pm_device_runtime_put(devc);
	zassert_equal(ret, 0, NULL);

	pm_device_state_get(domain, &state);
	zassert_equal(state, PM_DEVICE_STATE_SUSPENDED, NULL);

	/*
	 * Now lets test that devices are notified when the domain
	 * changes its state.
	 */

	/* Three devices has to get the notification */
	testing_domain_on_notitication = 3;
	ret = pm_device_runtime_get(domain);
	zassert_equal(ret, 0, NULL);

	zassert_equal(testing_domain_on_notitication, 0, NULL);

	testing_domain_off_notitication = 3;
	ret = pm_device_runtime_put(domain);
	zassert_equal(ret, 0, NULL);

	zassert_equal(testing_domain_off_notitication, 0, NULL);
}

void test_main(void)
{
	ztest_test_suite(power_domain_test,
			 ztest_1cpu_unit_test(test_power_domain_device_runtime));

	ztest_run_test_suite(power_domain_test);
}
