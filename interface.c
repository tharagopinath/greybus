/*
 * Greybus interface code
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"

/* interface sysfs attributes */
#define gb_interface_attr(field, type)					\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_interface *intf = to_gb_interface(dev);		\
	return scnprintf(buf, PAGE_SIZE, type"\n", intf->field);	\
}									\
static DEVICE_ATTR_RO(field)

gb_interface_attr(ddbl1_manufacturer_id, "0x%08x");
gb_interface_attr(ddbl1_product_id, "0x%08x");
gb_interface_attr(interface_id, "%u");
gb_interface_attr(vendor_id, "0x%08x");
gb_interface_attr(product_id, "0x%08x");
gb_interface_attr(vendor_string, "%s");
gb_interface_attr(product_string, "%s");
gb_interface_attr(serial_number, "0x%016llx");

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct gb_interface *intf = to_gb_interface(dev);

	return scnprintf(buf, PAGE_SIZE, "%u.%u\n", intf->version_major,
			 intf->version_minor);
}
static DEVICE_ATTR_RO(version);

static ssize_t power_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gb_interface *intf = to_gb_interface(dev);

	if (intf->pwr_state == INTERFACE_PWR_OFF)
		return scnprintf(buf, PAGE_SIZE, "%s\n", "OFF");
	else if (intf->pwr_state == INTERFACE_PWR_SUSPEND)
		return scnprintf(buf, PAGE_SIZE, "%s\n", "SUSPENDED");
	else
		return scnprintf(buf, PAGE_SIZE, "%s\n", "ON");
}
static DEVICE_ATTR_RO(power_state);

static struct attribute *interface_attrs[] = {
	&dev_attr_ddbl1_manufacturer_id.attr,
	&dev_attr_ddbl1_product_id.attr,
	&dev_attr_interface_id.attr,
	&dev_attr_vendor_id.attr,
	&dev_attr_product_id.attr,
	&dev_attr_vendor_string.attr,
	&dev_attr_product_string.attr,
	&dev_attr_serial_number.attr,
	&dev_attr_version.attr,
	&dev_attr_power_state.attr,
	NULL,
};
ATTRIBUTE_GROUPS(interface);


// FIXME, odds are you don't want to call this function, rework the caller to
// not need it please.
struct gb_interface *gb_interface_find(struct gb_host_device *hd,
				       u8 interface_id)
{
	struct gb_interface *intf;

	list_for_each_entry(intf, &hd->interfaces, links)
		if (intf->interface_id == interface_id)
			return intf;

	return NULL;
}

static void gb_interface_release(struct device *dev)
{
	struct gb_interface *intf = to_gb_interface(dev);

	kfree(intf->product_string);
	kfree(intf->vendor_string);

	if (intf->control)
		gb_control_destroy(intf->control);

	kfree(intf);
}

struct device_type greybus_interface_type = {
	.name =		"greybus_interface",
	.release =	gb_interface_release,
};

int gb_interface_pm_power_on(struct gb_interface *interface)
{
	int ret;

	/* interface is already powered on. Return doing nothing */
	if (interface->pwr_state == INTERFACE_PWR_ON)
		return 0;

	if (interface->pwr_state == INTERFACE_PWR_OFF) {
		/* Todo. If interface is powered on from PWR_OFF state, send the
		 * command to turn power on to the interface, turn ref clk on and
		 * any other sequence to aid the cold boot of the interface
	 	 */
	}

	else if (interface->pwr_state == INTERFACE_PWR_SUSPEND) {
		/* Todo. If interface is powered on from PWR_SUSPEND state,
		 * send the commands to send wake detect pulse to the
		 * interface, turn on refclk, and re-establish the previous
		 * connections
		 */
	}

	ret = gb_control_interface_power_state_set(interface,
							INTERFACE_PWR_ON);

	if (ret) {
		dev_err(&interface->dev, "Error trying to set INTF_PWR_ON \
					power state\n");
		return ret;
	}

	interface->pwr_state = INTERFACE_PWR_ON;
	return 0;
}

int gb_interface_pm_power_suspend(struct gb_interface *interface)
{
	int ret = 0;
	struct gb_bundle *bundle;
	struct gb_svc *svc = interface->hd->svc;

	/* Interface is already suspended. Return doing nothing */
	if (interface->pwr_state == INTERFACE_PWR_SUSPEND)
		return 0;

	/* Interface cannot be transitioned from PWR_OFF state to PWR_SUSPEND
	 * state. Return error.
	 */
	if (interface->pwr_state == INTERFACE_PWR_OFF) {
		dev_err(&interface->dev, "Trying to suspend the interface \
					when in off state \n");
		return -1;
	}

	/* Interface cannot be transitioned to PWR_SUSPEND state, if any
	 * bundle in the interface is in PWR_ON state. All bundles
	 * must be in PWR_SUSPEND state or PWR_OFF state.
	 */
	list_for_each_entry(bundle, &interface->bundles, links) {
		if (bundle->pwr_state == BUNDLE_PWR_ON)
			return 0;
	}

	/* Todo. Disable/disconnect the existing CPort connections except
	 * the control CPort as  PWR_SUSPEND state will lead to hibernate of
	 * unipro link which in turn will cause these connections to diappear
	 * in hardware
	 */

	ret = gb_control_interface_power_state_set(interface,
							INTERFACE_PWR_SUSPEND);

	if (ret) {
		/* Todo. May want to re-enable the disconnected CPort
		 * connections here as the suspend failed. Or may want to
		 * reset the interface as it could be in an unpredictable
		 * state
		 */
		dev_err(&interface->dev, "Error trying to set \
					INTERFACE_PWR_SUSPEND power state\n");
		return ret;
	}

	/* Todo. Disable the control CPort */

	/* Turn off reference clock */
	gb_svc_intf_refclk_state_set(svc, interface->interface_id, GB_SVC_INTF_REFCLK_DISABLE);

	interface->pwr_state = INTERFACE_PWR_SUSPEND;
	return 0;
}

int gb_interface_pm_power_off(struct gb_interface *interface)
{
	int ret = 0;
	struct gb_bundle *bundle;
	struct gb_svc *svc = interface->hd->svc;

	/* Interface is already powered off. Return doing nothing */
	if (interface->pwr_state == INTERFACE_PWR_OFF)
		return 0;

	/* Interface cannot be transitioned from PWR_SUSPEND state to PWR_OFF
	 * state. Return error.
	 */
	if (interface->pwr_state == INTERFACE_PWR_SUSPEND) {
		dev_err(&interface->dev, "Trying to power off the interface \
					when in suspend state \n");
		return -1;
	}

	/* Interface cannot be transitioned to PWR_OFF state, if any
	 * bundle in the interface is in PWR_ON or PWR_SUSPEND state.
	 * All bundles must be in PWR_OFF state.
	 */
	list_for_each_entry(bundle, &interface->bundles, links) {
		if ((bundle->pwr_state == BUNDLE_PWR_ON) ||
			(bundle->pwr_state == BUNDLE_PWR_SUSPEND))
			return 0;
	}

	/* Todo. Disable/disconnect the existing CPort connections except
	 * the control CPort as PWR_OFF state will lead to power down of
	 * unipro link which in turn will cause these connections to diappear
	 * in hardware.
	 */

	ret = gb_control_interface_power_state_set(interface,
							INTERFACE_PWR_OFF);

	if (ret) {
		/* Todo. May want to re-enable the disconnected CPort
		 * connections here as the power off failed. Or may want to
		 * reset the interface as it could be in an unpredictable
		 * state.
		 */
		dev_err(&interface->dev, "Error trying to set \
					INTERFACE_PWR_OFF power state\n");
		return ret;
	}

	/* Todo. Disable the control CPort */

	/* Turn off reference clock */
	gb_svc_intf_refclk_state_set(svc, interface->interface_id, GB_SVC_INTF_REFCLK_DISABLE);

	/* Turn off the interface power */
	gb_svc_intf_power_state_set(svc, interface->interface_id, GB_SVC_INTF_PWR_DISABLE);

	interface->pwr_state = INTERFACE_PWR_OFF;
	return 0;
}

/*
 * A Greybus module represents a user-replaceable component on an Ara
 * phone.  An interface is the physical connection on that module.  A
 * module may have more than one interface.
 *
 * Create a gb_interface structure to represent a discovered interface.
 * The position of interface within the Endo is encoded in "interface_id"
 * argument.
 *
 * Returns a pointer to the new interfce or a null pointer if a
 * failure occurs due to memory exhaustion.
 *
 * Locking: Caller ensures serialisation with gb_interface_remove and
 * gb_interface_find.
 */
struct gb_interface *gb_interface_create(struct gb_host_device *hd,
					 u8 interface_id)
{
	struct gb_interface *intf;

	intf = kzalloc(sizeof(*intf), GFP_KERNEL);
	if (!intf)
		return NULL;

	intf->hd = hd;		/* XXX refcount? */
	intf->interface_id = interface_id;
	INIT_LIST_HEAD(&intf->bundles);
	INIT_LIST_HEAD(&intf->manifest_descs);

	/* Invalid device id to start with */
	intf->device_id = GB_DEVICE_ID_BAD;

	intf->dev.parent = &hd->dev;
	intf->dev.bus = &greybus_bus_type;
	intf->dev.type = &greybus_interface_type;
	intf->dev.groups = interface_groups;
	intf->dev.dma_mask = hd->dev.dma_mask;
	device_initialize(&intf->dev);
	dev_set_name(&intf->dev, "%d-%d", hd->bus_id, interface_id);

	intf->control = gb_control_create(intf);
	if (!intf->control) {
		put_device(&intf->dev);
		return NULL;
	}

	list_add(&intf->links, &hd->interfaces);

	intf->pwr_state = INTERFACE_PWR_ON;

	return intf;
}

/*
 * Enable an interface by enabling its control connection and fetching the
 * manifest and other information over it.
 */
int gb_interface_enable(struct gb_interface *intf)
{
	struct gb_bundle *bundle, *tmp;
	int ret, size;
	void *manifest;

	/* Establish control connection */
	ret = gb_control_enable(intf->control);
	if (ret)
		return ret;

	/* Get manifest size using control protocol on CPort */
	size = gb_control_get_manifest_size_operation(intf);
	if (size <= 0) {
		dev_err(&intf->dev, "failed to get manifest size: %d\n", size);

		if (size)
			ret = size;
		else
			ret =  -EINVAL;

		goto err_disable_control;
	}

	manifest = kmalloc(size, GFP_KERNEL);
	if (!manifest) {
		ret = -ENOMEM;
		goto err_disable_control;
	}

	/* Get manifest using control protocol on CPort */
	ret = gb_control_get_manifest_operation(intf, manifest, size);
	if (ret) {
		dev_err(&intf->dev, "failed to get manifest: %d\n", ret);
		goto err_free_manifest;
	}

	/*
	 * Parse the manifest and build up our data structures representing
	 * what's in it.
	 */
	if (!gb_manifest_parse(intf, manifest, size)) {
		dev_err(&intf->dev, "failed to parse manifest\n");
		ret = -EINVAL;
		goto err_destroy_bundles;
	}

	ret = gb_control_get_interface_version_operation(intf);
	if (ret)
		goto err_destroy_bundles;

	ret = gb_control_get_bundle_versions(intf->control);
	if (ret)
		goto err_destroy_bundles;

	kfree(manifest);

	return 0;

err_destroy_bundles:
	list_for_each_entry_safe(bundle, tmp, &intf->bundles, links)
		gb_bundle_destroy(bundle);
err_free_manifest:
	kfree(manifest);
err_disable_control:
	gb_control_disable(intf->control);

	return ret;
}

/* Disable an interface and destroy its bundles. */
void gb_interface_disable(struct gb_interface *intf)
{
	struct gb_bundle *bundle;
	struct gb_bundle *next;

	/*
	 * Disable the control-connection early to avoid operation timeouts
	 * when the interface is already gone.
	 */
	if (intf->disconnected)
		gb_control_disable(intf->control);

	list_for_each_entry_safe(bundle, next, &intf->bundles, links)
		gb_bundle_destroy(bundle);

	gb_control_disable(intf->control);
}

/* Register an interface and its bundles. */
int gb_interface_add(struct gb_interface *intf)
{
	struct gb_bundle *bundle, *tmp;
	int ret;

	ret = device_add(&intf->dev);
	if (ret) {
		dev_err(&intf->dev, "failed to register interface: %d\n", ret);
		return ret;
	}

	dev_info(&intf->dev, "Interface added: VID=0x%08x, PID=0x%08x\n",
		 intf->vendor_id, intf->product_id);
	dev_info(&intf->dev, "DDBL1 Manufacturer=0x%08x, Product=0x%08x\n",
		 intf->ddbl1_manufacturer_id, intf->ddbl1_product_id);

	list_for_each_entry_safe_reverse(bundle, tmp, &intf->bundles, links) {
		ret = gb_bundle_add(bundle);
		if (ret) {
			gb_bundle_destroy(bundle);
			continue;
		}
	}

	return 0;
}

/* Deregister an interface and drop its reference. */
void gb_interface_remove(struct gb_interface *intf)
{
	if (device_is_registered(&intf->dev)) {
		device_del(&intf->dev);
		dev_info(&intf->dev, "Interface removed\n");
	}

	list_del(&intf->links);

	put_device(&intf->dev);
}
