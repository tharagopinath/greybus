/*
 * Greybus bundles
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

static ssize_t bundle_class_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	return sprintf(buf, "0x%02x\n", bundle->class);
}
static DEVICE_ATTR_RO(bundle_class);

static ssize_t bundle_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	return sprintf(buf, "%u\n", bundle->id);
}
static DEVICE_ATTR_RO(bundle_id);

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	if (bundle->state == NULL)
		return sprintf(buf, "\n");

	return sprintf(buf, "%s\n", bundle->state);
}

static ssize_t state_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t size)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	kfree(bundle->state);
	bundle->state = kstrdup(buf, GFP_KERNEL);
	if (!bundle->state)
		return -ENOMEM;

	/* Tell userspace that the file contents changed */
	sysfs_notify(&bundle->dev.kobj, NULL, "state");

	return size;
}
static DEVICE_ATTR_RW(state);

static ssize_t power_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	if (bundle->pwr_state == BUNDLE_PWR_OFF)
		return scnprintf(buf, PAGE_SIZE, "%s\n", "OFF");
	else if (bundle->pwr_state == BUNDLE_PWR_SUSPEND)
		return scnprintf(buf, PAGE_SIZE, "%s\n", "SUSPENDED");
	else
		return scnprintf(buf, PAGE_SIZE, "%s\n", "ON");
}
static DEVICE_ATTR_RO(power_state);

static struct attribute *bundle_attrs[] = {
	&dev_attr_bundle_class.attr,
	&dev_attr_bundle_id.attr,
	&dev_attr_state.attr,
	&dev_attr_power_state.attr,
	NULL,
};

ATTRIBUTE_GROUPS(bundle);

static struct gb_bundle *gb_bundle_find(struct gb_interface *intf,
							u8 bundle_id)
{
	struct gb_bundle *bundle;

	list_for_each_entry(bundle, &intf->bundles, links) {
		if (bundle->id == bundle_id)
			return bundle;
	}

	return NULL;
}

static void gb_bundle_release(struct device *dev)
{
	struct gb_bundle *bundle = to_gb_bundle(dev);

	kfree(bundle->state);
	kfree(bundle->cport_desc);
	kfree(bundle);
}

int gb_bundle_pm_power_on(struct gb_bundle *bundle)
{
	int ret;

	/*Bundle is already powered on. Return doing nothing */
	if (bundle->pwr_state == BUNDLE_PWR_ON)
		return 0;

	/* Interface should be powered on before powering on bundle */
	ret = gb_interface_pm_power_on(bundle->intf);

	if (ret) {
		dev_err(&bundle->dev, "Error trying on power on the parent \
					interface\n");
		return ret;
	}

	ret = gb_control_bundle_power_state_set(bundle, BUNDLE_PWR_ON);

	if (ret) {
		dev_err(&bundle->dev, "Error trying to set BUNDLE_PWR_ON \
					power state\n");
		return ret;
	}

	bundle->pwr_state = BUNDLE_PWR_ON;
	return 0;
}

int gb_bundle_pm_power_suspend(struct gb_bundle *bundle)
{
	int ret = 0;
	struct gb_connection *connection;

	/* Bundle is already suspended. Return doing nothing */
	if (bundle->pwr_state == BUNDLE_PWR_SUSPEND)
		return 0;

	/* Bundle cannot be transitioned from PWR_OFF state to PWR_SUSPEND
	 * state. Return error.
	 */
	if (bundle->pwr_state == BUNDLE_PWR_OFF) {
		dev_err(&bundle->dev, "Trying to suspend the bundle when in \
					off state \n");
		return -1;
	}

	/* Bundle cannot be transitioned to PWR_SUSPEND state, if any
	 * connection in the bundle is in PWR_ON state. All connections
	 * must be in PWR_SUSPEND or PWR_OFF state.
	 */
	list_for_each_entry(connection, &bundle->connections, bundle_links) {
		if (connection->pwr_state == CONNECTION_PWR_ON)
			return 0;
	}

	ret = gb_control_bundle_power_state_set(bundle, BUNDLE_PWR_SUSPEND);
	if (ret) {
		dev_err(&bundle->dev, "Error trying to set BUNDLE_PWR_SUSPEND \
					power state\n");
		return ret;
	}

	bundle->pwr_state = BUNDLE_PWR_SUSPEND;

	/* Try to suspend bundle's interface. This may not succeed if
	 * the Interface has other bundles that are still powered on.
	 */
	gb_interface_pm_power_suspend(bundle->intf);

	return 0;
}

int gb_bundle_pm_power_off(struct gb_bundle *bundle)
{
	int ret = 0;
	struct gb_connection *connection;


	/* Bundle is already powered off. Return doing nothing */
	if (bundle->pwr_state == BUNDLE_PWR_OFF)
		return 0;

	/* Bundle cannot be transitioned from PWR_SUSPEND state to PWR_OFF
	 * state. Return error
	 */
	if (bundle->pwr_state == BUNDLE_PWR_SUSPEND) {
		dev_err(&bundle->dev, "Trying to power off the bundle when in \
					suspend state \n");
		return -1;
	}

	/* Bundle cannot be transitioned to PWR_OFF state, if any connection
	 * in the bundle is in PWR_ON or PWR_SUSPEND state. All connections
	 * must be in PWR_OFF state.
	 */
	list_for_each_entry(connection, &bundle->connections, bundle_links) {
		if ((connection->pwr_state == CONNECTION_PWR_ON) ||
			(connection->pwr_state == CONNECTION_PWR_SUSPEND))
			return 0;
	}

	ret = gb_control_bundle_power_state_set(bundle, BUNDLE_PWR_OFF);
	if (ret) {
		dev_err(&bundle->dev, "Error trying to set BUNDLE_PWR_OFF \
					power state\n");
		return ret;
	}

	bundle->pwr_state = BUNDLE_PWR_OFF;

	/* Try to power off bundle's interface. This may not succeed if
	 * the Interface has other bundles that are still powered on or
	 * suspended.
	 */
	gb_interface_pm_power_off(bundle->intf);

	return 0;
}

static int gb_bundle_suspend(struct device *dev)
{
	int ret;
	struct gb_connection *connection;
	struct gb_bundle *bundle = to_gb_bundle(dev);

	/* Notify all the connections about the suspend */
	list_for_each_entry(connection, &bundle->connections, bundle_links) {
		if (connection->suspend) {
			ret = connection->suspend(connection);

			if (ret) {
				dev_err(dev, "Error in bundle suspend \n");
				return ret;
			}
		}
	}

	/* If bundle is in suspend state, power it on before powering
	 * it off. This is essential when runtime pm comes into picture.
	 */
	if (bundle->pwr_state == BUNDLE_PWR_SUSPEND) {
		ret = gb_bundle_pm_power_on(bundle);

		if (ret) {
			dev_err(dev, "Error in bundle suspend \n");
			return ret;
		}
	}

	return gb_bundle_pm_power_off(bundle);
}

static const struct dev_pm_ops bundle_pm_ops = {
	.suspend = &gb_bundle_suspend,
};

struct device_type greybus_bundle_type = {
	.name =		"greybus_bundle",
	.release =	gb_bundle_release,
	.pm =		&bundle_pm_ops,
};

/*
 * Create a gb_bundle structure to represent a discovered
 * bundle.  Returns a pointer to the new bundle or a null
 * pointer if a failure occurs due to memory exhaustion.
 */
struct gb_bundle *gb_bundle_create(struct gb_interface *intf, u8 bundle_id,
				   u8 class)
{
	struct gb_bundle *bundle;

	/*
	 * Reject any attempt to reuse a bundle id.  We initialize
	 * these serially, so there's no need to worry about keeping
	 * the interface bundle list locked here.
	 */
	if (gb_bundle_find(intf, bundle_id)) {
		dev_err(&intf->dev, "duplicate bundle id %u\n", bundle_id);
		return NULL;
	}

	bundle = kzalloc(sizeof(*bundle), GFP_KERNEL);
	if (!bundle)
		return NULL;

	bundle->intf = intf;
	bundle->id = bundle_id;
	bundle->class = class;
	INIT_LIST_HEAD(&bundle->connections);

	bundle->dev.parent = &intf->dev;
	bundle->dev.bus = &greybus_bus_type;
	bundle->dev.type = &greybus_bundle_type;
	bundle->dev.groups = bundle_groups;
	device_initialize(&bundle->dev);
	dev_set_name(&bundle->dev, "%s.%d", dev_name(&intf->dev), bundle_id);

	list_add(&bundle->links, &intf->bundles);

	bundle->pwr_state = BUNDLE_PWR_ON;

	return bundle;
}

int gb_bundle_add(struct gb_bundle *bundle)
{
	int ret;

	ret = device_add(&bundle->dev);
	if (ret) {
		dev_err(&bundle->dev, "failed to register bundle: %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * Tear down a previously set up bundle.
 */
void gb_bundle_destroy(struct gb_bundle *bundle)
{
	if (device_is_registered(&bundle->dev))
		device_del(&bundle->dev);

	list_del(&bundle->links);

	put_device(&bundle->dev);
}
