/*
 * MinimalPad host module: combined USB transport
 *
 * ZMK Studio RPC already uses a framed byte stream on one CDC-ACM UART. The
 * MinimalPad host protocol shares that same stream. Studio frames begin with
 * 0xAB and end with an unescaped 0xAD; a host frame begins with its version and
 * declares its complete length in byte 2. The two grammars are therefore
 * unambiguous without another USB interface or another serial port.
 *
 * One physical stream is also the identity boundary: core.getDeviceInfo and
 * every Profile write necessarily reach the same pad.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>

#include <zmk/endpoints_types.h>
#include <zmk/studio/rpc.h>

#include <minimalpad/host.h>

LOG_MODULE_DECLARE(minimalpad_host, CONFIG_MINIMALPAD_HOST_LOG_LEVEL);

#define UART_NODE DT_CHOSEN(zmk_studio_rpc_uart)
#define STUDIO_SOF 0xAB
#define STUDIO_ESC 0xAC
#define STUDIO_EOF 0xAD

#define RX_CHUNK_SIZE 64
#define RX_QUEUE_LEN 8
#define TX_WAIT K_MSEC(500)

static const struct device *const uart_dev = DEVICE_DT_GET(UART_NODE);

struct usb_rx_chunk {
    uint8_t len;
    uint8_t data[RX_CHUNK_SIZE];
};

K_MSGQ_DEFINE(mp_host_usb_rx, sizeof(struct usb_rx_chunk), RX_QUEUE_LEN, 1);
RING_BUF_DECLARE(mp_host_usb_tx, CONFIG_MINIMALPAD_HOST_USB_TX_BUFFER_SIZE);
static K_MUTEX_DEFINE(mp_host_usb_tx_mutex);
static K_SEM_DEFINE(mp_host_usb_tx_drained, 0, 1);

static struct mp_host_reader host_reader;
static atomic_t selected;
static atomic_t host_seen;

enum rx_route {
    RX_IDLE,
    RX_STUDIO,
    RX_HOST,
};

static enum rx_route rx_route;
static bool studio_escaped;

/*
 * Copy to the combined transmit ring while the caller holds
 * mp_host_usb_tx_mutex. The UART callback is its single consumer. Waiting is
 * bounded so an unplug cannot pin the system work queue forever.
 */
static int wire_write_locked(const uint8_t *bytes, size_t len) {
    size_t written = 0;

    while (written < len) {
        const uint32_t added = ring_buf_put(&mp_host_usb_tx, bytes + written, len - written);
        written += added;
        uart_irq_tx_enable(uart_dev);

        if (written < len && k_sem_take(&mp_host_usb_tx_drained, TX_WAIT) != 0) {
            LOG_WRN("USB transmit buffer did not drain");
            return -ETIMEDOUT;
        }
    }

    return 0;
}

static int usb_send(const uint8_t *frame, size_t len) {
    if (!atomic_get(&selected) || !atomic_get(&host_seen)) {
        return -ENOTCONN;
    }

    k_mutex_lock(&mp_host_usb_tx_mutex, K_FOREVER);
    const int err = wire_write_locked(frame, len);
    k_mutex_unlock(&mp_host_usb_tx_mutex);
    return err;
}

static bool usb_sender_may_command(void) { return atomic_get(&selected); }

const struct mp_host_transport mp_host_transport_usb = {
    .name = "USB",
    .send = usb_send,
    .reply = usb_send,
    .sender_may_command = usb_sender_may_command,
};

/*
 * ZMK encodes one response into its small TX ring in several callbacks. Hold
 * the producer mutex from SOF through EOF so a host STATE can never land in
 * the middle of that Studio response.
 */
static bool studio_message_locked;

static void studio_tx_notify(struct ring_buf *rpc, size_t added, bool message_done,
                             void *user_data) {
    ARG_UNUSED(added);
    ARG_UNUSED(user_data);

    if (!studio_message_locked) {
        k_mutex_lock(&mp_host_usb_tx_mutex, K_FOREVER);
        studio_message_locked = true;
    }

    while (ring_buf_size_get(rpc) > 0) {
        uint8_t *bytes;
        const uint32_t claim = ring_buf_get_claim(rpc, &bytes, ring_buf_size_get(rpc));
        if (claim == 0) {
            break;
        }

        const int err = wire_write_locked(bytes, claim);
        // Always release the RPC ring claim. Keeping failed bytes there would
        // make the protobuf encoder spin forever after an unplug.
        ring_buf_get_finish(rpc, claim);
        if (err < 0) {
            LOG_WRN("Dropping %u Studio response bytes (%d)", claim, err);
        }
    }

    if (message_done) {
        studio_message_locked = false;
        k_mutex_unlock(&mp_host_usb_tx_mutex);
    }
}

static bool studio_rx_put(uint8_t byte) {
    struct ring_buf *rpc = zmk_rpc_get_rx_buf();
    if (ring_buf_put(rpc, &byte, 1) == 1) {
        return true;
    }

    LOG_WRN("Dropping a Studio request byte; RPC RX buffer is full");
    return false;
}

static void route_byte(uint8_t byte, bool *studio_received) {
    switch (rx_route) {
    case RX_IDLE:
        if (byte == STUDIO_SOF) {
            rx_route = RX_STUDIO;
            studio_escaped = false;
            *studio_received |= studio_rx_put(byte);
        } else {
            rx_route = RX_HOST;
            atomic_set(&host_seen, 1);
            mp_host_reader_feed(&host_reader, &mp_host_transport_usb, &byte, 1);
            if (host_reader.len == 0) {
                rx_route = RX_IDLE;
            }
        }
        break;

    case RX_STUDIO:
        *studio_received |= studio_rx_put(byte);
        if (studio_escaped) {
            studio_escaped = false;
        } else if (byte == STUDIO_ESC) {
            studio_escaped = true;
        } else if (byte == STUDIO_EOF) {
            rx_route = RX_IDLE;
        } else if (byte == STUDIO_SOF) {
            // ZMK's decoder treats this as a replacement frame start too.
            studio_escaped = false;
        }
        break;

    case RX_HOST:
        mp_host_reader_feed(&host_reader, &mp_host_transport_usb, &byte, 1);
        if (host_reader.len == 0) {
            rx_route = RX_IDLE;
        }
        break;
    }
}

static void read_usb(struct k_work *work) {
    ARG_UNUSED(work);
    struct usb_rx_chunk chunk;

    while (k_msgq_get(&mp_host_usb_rx, &chunk, K_NO_WAIT) == 0) {
        bool studio_received = false;
        for (uint8_t i = 0; i < chunk.len; i++) {
            route_byte(chunk.data[i], &studio_received);
        }
        if (studio_received) {
            zmk_rpc_rx_notify();
        }
    }
}

static K_WORK_DEFINE(read_usb_work, read_usb);

static void queue_usb_rx(void) {
    struct usb_rx_chunk chunk;

    while (true) {
        const int count = uart_fifo_read(uart_dev, chunk.data, sizeof(chunk.data));
        if (count <= 0) {
            return;
        }
        chunk.len = count;

        while (k_msgq_put(&mp_host_usb_rx, &chunk, K_NO_WAIT) != 0) {
            struct usb_rx_chunk oldest;
            (void)k_msgq_get(&mp_host_usb_rx, &oldest, K_NO_WAIT);
            LOG_WRN("USB receive queue full, dropping the oldest chunk");
        }
        k_work_submit(&read_usb_work);

        if (count < sizeof(chunk.data)) {
            return;
        }
    }
}

static void drain_usb_tx(void) {
    while (ring_buf_size_get(&mp_host_usb_tx) > 0) {
        uint8_t *bytes;
        const uint32_t claim = ring_buf_get_claim(
            &mp_host_usb_tx, &bytes, ring_buf_size_get(&mp_host_usb_tx));
        if (claim == 0) {
            break;
        }

        const int sent = uart_fifo_fill(uart_dev, bytes, claim);
        ring_buf_get_finish(&mp_host_usb_tx, MAX(sent, 0));
        if (sent <= 0) {
            break;
        }
        k_sem_give(&mp_host_usb_tx_drained);
    }

    if (ring_buf_is_empty(&mp_host_usb_tx)) {
        uart_irq_tx_disable(uart_dev);
    }
}

static void serial_cb(const struct device *dev, void *user_data) {
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    if (!uart_irq_update(uart_dev)) {
        return;
    }
    if (uart_irq_rx_ready(uart_dev)) {
        queue_usb_rx();
    }
    if (uart_irq_tx_ready(uart_dev)) {
        drain_usb_tx();
    }
}

static int start_rx(void) {
    atomic_set(&selected, 1);
    uart_irq_rx_enable(uart_dev);
    return 0;
}

static int stop_rx(void) {
    atomic_set(&selected, 0);
    atomic_set(&host_seen, 0);
    uart_irq_rx_disable(uart_dev);
    k_msgq_purge(&mp_host_usb_rx);
    mp_host_reader_reset(&host_reader);
    rx_route = RX_IDLE;
    studio_escaped = false;
    return 0;
}

ZMK_RPC_TRANSPORT(minimalpad_usb, ZMK_TRANSPORT_USB, start_rx, stop_rx, NULL,
                  studio_tx_notify);

static int usb_transport_init(void) {
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("Combined USB UART is not ready");
        return -ENODEV;
    }

    const int err = uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
    if (err < 0) {
        LOG_ERR("Could not install the combined USB UART callback (%d)", err);
        return err;
    }

    return 0;
}

SYS_INIT(usb_transport_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
