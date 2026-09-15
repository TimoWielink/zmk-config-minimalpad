/*
 * Minimalpad host module: the Bluetooth transport
 *
 * Our own GATT service, built like ZMK Studio's (studio/gatt_rpc_transport.c):
 * the host writes frames to one characteristic and the pad notifies frames on
 * another. host-protocol.md, "Transports", and ADR 0007 say why it is not HID.
 *
 * Writes arrive on the Bluetooth RX thread. They are copied into a queue and
 * read on the system work queue, where the rest of the module runs, the way
 * Studio's transport hands its bytes to the RPC thread.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/ble.h>
#include <zmk/endpoints.h>

#include <minimalpad/host.h>

LOG_MODULE_DECLARE(minimalpad_host, CONFIG_MINIMALPAD_HOST_LOG_LEVEL);

/*
 * The longest write accepted. A write may hold more than one frame, since the
 * reader steps from one to the next; three is more than a host has reason to
 * pack. (This build's ATT MTU of 65 caps a write at 62 bytes anyway.)
 */
#define WRITE_MAX (3 * MP_HOST_FRAME_MAX)

/* Writes waiting for the work queue, which drains them within milliseconds. */
#define WRITE_QUEUE_LEN 4

struct host_write {
    /* The host that wrote it, referenced while the write is queued. */
    struct bt_conn *conn;
    /* Whether that host was the active profile's when it wrote. */
    bool active;
    uint8_t len;
    uint8_t data[WRITE_MAX];
};

K_MSGQ_DEFINE(mp_host_gatt_writes, sizeof(struct host_write), WRITE_QUEUE_LEN, 1);

static struct mp_host_reader reader;

/* The write being read, so answers go back to the host that sent it. Only the
 * work queue touches these. */
static struct bt_conn *sender;
static bool sender_active;

static void read_writes(struct k_work *work) {
    struct host_write entry;

    while (k_msgq_get(&mp_host_gatt_writes, &entry, K_NO_WAIT) == 0) {
        sender = entry.conn;
        sender_active = entry.active;

        mp_host_reader_feed(&reader, &mp_host_transport_gatt, entry.data, entry.len);

        // Every frame fits one packet, so bytes left at the end of a write are a frame cut
        // short, never the start of one the next write finishes.
        mp_host_reader_reset(&reader);

        sender = NULL;
        sender_active = false;
        bt_conn_unref(entry.conn);
    }
}

static K_WORK_DEFINE(read_writes_work, read_writes);

/*
 * Only the host the pad is typing into may change it. After a profile change the
 * previous host can stay connected, and its app switches must not change layers
 * under the new one (ADR 0005). ble.c tells the active profile's connection
 * apart the same way. frame.c decides per command, since HELLO and GET_STATE
 * change nothing and any bonded host gets an answer.
 */
static bool from_active_profile(struct bt_conn *conn) {
    return zmk_endpoint_get_selected().transport == ZMK_TRANSPORT_BLE &&
           bt_addr_le_cmp(bt_conn_get_dst(conn), zmk_ble_active_profile_addr()) == 0;
}

static ssize_t write_command(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    if (offset != 0) {
        // Frames are never split across writes, so there is nothing to continue.
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len > WRITE_MAX) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    struct host_write entry = {
        .conn = bt_conn_ref(conn),
        .active = from_active_profile(conn),
        .len = len,
    };
    memcpy(entry.data, buf, len);

    // "The newest SET_PROFILE wins": when a burst fills the queue, the oldest write makes room.
    while (k_msgq_put(&mp_host_gatt_writes, &entry, K_NO_WAIT) != 0) {
        struct host_write oldest;

        if (k_msgq_get(&mp_host_gatt_writes, &oldest, K_NO_WAIT) == 0) {
            bt_conn_unref(oldest.conn);
        }
        LOG_WRN("Host write queue full, dropping the oldest write");
    }

    k_work_submit(&read_writes_work);

    return len;
}

static void events_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    ARG_UNUSED(attr);

    const bool enabled = (value & BT_GATT_CCC_NOTIFY) != 0;

    LOG_INF("Host events %s", enabled ? "enabled" : "disabled");

    if (enabled) {
        mp_host_host_arrived(&mp_host_transport_gatt);
    }
}

/*
 * The command characteristic takes writes with and without response, so the D0
 * spike can measure which one switching should use (architecture.md, "Open
 * research").
 *
 * The event characteristic notifies rather than indicates: every frame fits one
 * packet, so there is nothing to reassemble, and the link layer already
 * retransmits. It has no read property; READ_ENCRYPT on the value and on its CCC
 * is what makes Zephyr refuse to notify, or accept a subscription, over a link
 * that is not encrypted. With WRITE_ENCRYPT on the commands, only a bonded host
 * can talk, as with ZMK Studio.
 */
BT_GATT_SERVICE_DEFINE(
    mp_host_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(MP_HOST_SERVICE_UUID)),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(MP_HOST_CMD_CHRC_UUID),
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE_ENCRYPT, NULL, write_command, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(MP_HOST_EVT_CHRC_UUID), BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, NULL, NULL, NULL),
    BT_GATT_CCC(events_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT));

/* attrs: 0 the service, 1 and 2 the command characteristic, 3 and 4 the event
 * characteristic, 5 its CCC. */
#define EVENT_VALUE_ATTR (&mp_host_service.attrs[4])

static int gatt_send(const uint8_t *frame, size_t len) {
    // Every subscribed host hears events, not only the active profile's: a FALLBACK for a
    // profile change is news above all to the host that just lost the pad. Zephyr answers
    // -ENOTCONN when nobody is subscribed.
    return bt_gatt_notify(NULL, EVENT_VALUE_ATTR, frame, len);
}

static int gatt_reply(const uint8_t *frame, size_t len) {
    if (!sender) {
        return -ENOTCONN;
    }

    // Zephyr refuses to notify a host that has not subscribed to events
    // (CONFIG_BT_GATT_ENFORCE_SUBSCRIPTION), which is right: it is not listening.
    return bt_gatt_notify(sender, EVENT_VALUE_ATTR, frame, len);
}

static bool gatt_sender_may_command(void) { return sender_active; }

const struct mp_host_transport mp_host_transport_gatt = {
    .name = "Bluetooth",
    .send = gatt_send,
    .reply = gatt_reply,
    .sender_may_command = gatt_sender_may_command,
};
