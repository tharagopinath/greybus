/*
 * Greybus SVC code
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __SVC_H
#define __SVC_H

#define GB_SVC_CPORT_FLAG_E2EFC       BIT(0)
#define GB_SVC_CPORT_FLAG_CSD_N       BIT(1)
#define GB_SVC_CPORT_FLAG_CSV_N       BIT(2)

enum gb_svc_state {
	GB_SVC_STATE_RESET,
	GB_SVC_STATE_PROTOCOL_VERSION,
	GB_SVC_STATE_SVC_HELLO,
};

struct gb_svc_watchdog;

struct gb_svc {
	struct device		dev;

	struct gb_host_device	*hd;
	struct gb_connection	*connection;
	enum gb_svc_state	state;
	struct ida		device_id_map;
	struct workqueue_struct	*wq;

	u16 endo_id;
	u8 ap_intf_id;

	u8 protocol_major;
	u8 protocol_minor;

	struct input_dev        *input;
	char                    *input_phys;
	struct gb_svc_watchdog	*watchdog;
};
#define to_gb_svc(d) container_of(d, struct gb_svc, d)

struct gb_svc *gb_svc_create(struct gb_host_device *hd);
int gb_svc_add(struct gb_svc *svc);
void gb_svc_del(struct gb_svc *svc);
void gb_svc_put(struct gb_svc *svc);

int gb_svc_intf_reset(struct gb_svc *svc, u8 intf_id);
int gb_svc_connection_create(struct gb_svc *svc, u8 intf1_id, u16 cport1_id,
			     u8 intf2_id, u16 cport2_id, u8 cport_flags);
void gb_svc_connection_destroy(struct gb_svc *svc, u8 intf1_id, u16 cport1_id,
			       u8 intf2_id, u16 cport2_id);
int gb_svc_intf_eject(struct gb_svc *svc, u8 intf_id);
int gb_svc_dme_peer_get(struct gb_svc *svc, u8 intf_id, u16 attr, u16 selector,
			u32 *value);
int gb_svc_dme_peer_set(struct gb_svc *svc, u8 intf_id, u16 attr, u16 selector,
			u32 value);
int gb_svc_intf_set_power_mode(struct gb_svc *svc, u8 intf_id, u8 hs_series,
			       u8 tx_mode, u8 tx_gear, u8 tx_nlanes,
			       u8 rx_mode, u8 rx_gear, u8 rx_nlanes,
			       u8 flags, u32 quirks);
int gb_svc_ping(struct gb_svc *svc);
int gb_svc_watchdog_create(struct gb_svc *svc);
void gb_svc_watchdog_destroy(struct gb_svc *svc);
bool gb_svc_watchdog_enabled(struct gb_svc *svc);
int gb_svc_watchdog_enable(struct gb_svc *svc);
int gb_svc_watchdog_disable(struct gb_svc *svc);

int gb_svc_protocol_init(void);
void gb_svc_protocol_exit(void);

#endif /* __SVC_H */
