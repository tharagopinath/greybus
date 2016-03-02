/*
 * Greybus CPort control protocol
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __CONTROL_H
#define __CONTROL_H

struct gb_control {
	struct gb_connection	*connection;

	u8 protocol_major;
	u8 protocol_minor;

	bool has_bundle_version;
};

struct gb_control *gb_control_create(struct gb_interface *intf);
int gb_control_enable(struct gb_control *control);
void gb_control_disable(struct gb_control *control);
void gb_control_destroy(struct gb_control *control);

int gb_control_get_bundle_versions(struct gb_control *control);
int gb_control_connected_operation(struct gb_control *control, u16 cport_id);
int gb_control_disconnected_operation(struct gb_control *control, u16 cport_id);
int gb_control_get_manifest_size_operation(struct gb_interface *intf);
int gb_control_get_manifest_operation(struct gb_interface *intf, void *manifest,
				      size_t size);
int gb_control_get_interface_version_operation(struct gb_interface *intf);
int gb_control_interface_set_power_state(struct gb_interface *intf, u8 pwr_state);

#endif /* __CONTROL_H */
