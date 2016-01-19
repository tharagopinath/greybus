/*
 * Greybus CPort control protocol.
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "greybus.h"

/* Get Manifest's size from the interface */
int gb_control_get_manifest_size_operation(struct gb_interface *intf)
{
	struct gb_control_get_manifest_size_response response;
	struct gb_connection *connection = intf->control->connection;
	int ret;

	ret = gb_operation_sync(connection, GB_CONTROL_TYPE_GET_MANIFEST_SIZE,
				NULL, 0, &response, sizeof(response));
	if (ret) {
		dev_err(&connection->intf->dev,
				"failed to get manifest size: %d\n", ret);
		return ret;
	}

	return le16_to_cpu(response.size);
}

/* Reads Manifest from the interface */
int gb_control_get_manifest_operation(struct gb_interface *intf, void *manifest,
				      size_t size)
{
	struct gb_connection *connection = intf->control->connection;

	return gb_operation_sync(connection, GB_CONTROL_TYPE_GET_MANIFEST,
				NULL, 0, manifest, size);
}

int gb_control_connected_operation(struct gb_control *control, u16 cport_id)
{
	struct gb_control_connected_request request;

	request.cport_id = cpu_to_le16(cport_id);
	return gb_operation_sync(control->connection, GB_CONTROL_TYPE_CONNECTED,
				 &request, sizeof(request), NULL, 0);
}

int gb_control_disconnected_operation(struct gb_control *control, u16 cport_id)
{
	struct gb_control_disconnected_request request;

	request.cport_id = cpu_to_le16(cport_id);
	return gb_operation_sync(control->connection,
				 GB_CONTROL_TYPE_DISCONNECTED, &request,
				 sizeof(request), NULL, 0);
}

int gb_control_get_interface_version_operation(struct gb_interface *intf)
{
	struct gb_control_interface_version_response response;
	struct gb_connection *connection = intf->control->connection;
	int ret;

	/* The ES3 bootrom fails to boot if this request it sent to it */
	if (intf->boot_over_unipro)
		return 0;

	ret = gb_operation_sync(connection, GB_CONTROL_TYPE_INTERFACE_VERSION,
				NULL, 0, &response, sizeof(response));
	if (ret) {
		dev_err(&connection->intf->dev,
			"failed to get interface version: %d\n", ret);
		/*
		 * FIXME: Return success until the time we bump version of
		 * control protocol. The interface-version is already set to
		 * 0.0, so no need to update that.
		 */
		return 0;
	}

	intf->version_major = le16_to_cpu(response.major);
	intf->version_minor = le16_to_cpu(response.minor);

	return 0;
}

struct gb_control *gb_control_create(struct gb_interface *intf)
{
	struct gb_control *control;

	control = kzalloc(sizeof(*control), GFP_KERNEL);
	if (!control)
		return NULL;

	control->connection = gb_connection_create_dynamic(intf, NULL,
						GB_CONTROL_CPORT_ID,
						GREYBUS_PROTOCOL_CONTROL);
	if (!control->connection) {
		dev_err(&intf->dev, "failed to create control connection\n");
		kfree(control);
		return NULL;
	}

	control->connection->private = control;

	return control;
}

int gb_control_enable(struct gb_control *control)
{
	int ret;

	dev_dbg(&control->connection->intf->dev, "%s\n", __func__);

	ret = gb_connection_legacy_init(control->connection);
	if (ret) {
		dev_err(&control->connection->intf->dev,
				"failed to enable control connection: %d\n",
				ret);
		return ret;
	}

	return 0;
}

void gb_control_disable(struct gb_control *control)
{
	dev_dbg(&control->connection->intf->dev, "%s\n", __func__);

	gb_connection_legacy_exit(control->connection);
}

void gb_control_destroy(struct gb_control *control)
{
	gb_connection_destroy(control->connection);
	kfree(control);
}

static int gb_control_connection_init(struct gb_connection *connection)
{
	dev_dbg(&connection->intf->dev, "%s\n", __func__);

	return 0;
}

static void gb_control_connection_exit(struct gb_connection *connection)
{
	dev_dbg(&connection->intf->dev, "%s\n", __func__);
}

static struct gb_protocol control_protocol = {
	.name			= "control",
	.id			= GREYBUS_PROTOCOL_CONTROL,
	.major			= GB_CONTROL_VERSION_MAJOR,
	.minor			= GB_CONTROL_VERSION_MINOR,
	.connection_init	= gb_control_connection_init,
	.connection_exit	= gb_control_connection_exit,
	.flags			= GB_PROTOCOL_SKIP_CONTROL_CONNECTED |
				  GB_PROTOCOL_SKIP_CONTROL_DISCONNECTED,
};
gb_builtin_protocol_driver(control_protocol);
