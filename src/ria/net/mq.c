/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef RP6502_RIA_W
#include "net/mq.h"
void mq_task(void) {}
void mq_stop(void) {}
void mq_init(void) {}
bool mq_api_connect(void) { return false; }
bool mq_api_disconnect(void) { return false; }
bool mq_api_publish(void) { return false; }
bool mq_api_subscribe(void) { return false; }
bool mq_api_unsubscribe(void) { return false; }
bool mq_api_poll(void) { return false; }
bool mq_api_read_message(void) { return false; }
bool mq_api_get_topic(void) { return false; }
bool mq_api_connected(void) { return false; }
bool mq_api_set_auth(void) { return false; }
bool mq_api_set_will(void) { return false; }
#else

#include "net/mq.h"
#include "api/api.h"
#include "sys/mem.h"
#include <pico/time.h>
#include <lwip/tcp.h>
#include <lwip/dns.h>
#include <string.h>

#define DEBUG_RIA_NET 1

#if defined(DEBUG_RIA_NET) || defined(DEBUG_RIA_NET_MQ)
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void DBG(const char *fmt, ...) { (void)fmt; }
#endif

/* MQTT Protocol Constants */
#define MQTT_PROTOCOL_VERSION 4  // MQTT 3.1.1

// MQTT Control Packet Types
#define MQTT_MSG_TYPE_CONNECT     1
#define MQTT_MSG_TYPE_CONNACK     2
#define MQTT_MSG_TYPE_PUBLISH     3
#define MQTT_MSG_TYPE_PUBACK      4
#define MQTT_MSG_TYPE_PUBREC      5
#define MQTT_MSG_TYPE_PUBREL      6
#define MQTT_MSG_TYPE_PUBCOMP     7
#define MQTT_MSG_TYPE_SUBSCRIBE   8
#define MQTT_MSG_TYPE_SUBACK      9
#define MQTT_MSG_TYPE_UNSUBSCRIBE 10
#define MQTT_MSG_TYPE_UNSUBACK    11
#define MQTT_MSG_TYPE_PINGREQ     12
#define MQTT_MSG_TYPE_PINGRESP    13
#define MQTT_MSG_TYPE_DISCONNECT  14

// MQTT QoS levels
#define MQTT_QOS_0 0
#define MQTT_QOS_1 1
#define MQTT_QOS_2 2

// Buffer sizes
#define MQTT_TX_BUF_SIZE 1024
#define MQTT_RX_BUF_SIZE 2048
#define MQTT_TOPIC_BUF_SIZE 256
#define MQTT_PAYLOAD_BUF_SIZE 1024
#define MQTT_CLIENT_ID_MAX 128
#define MQTT_USERNAME_MAX 128
#define MQTT_PASSWORD_MAX 128

// Timing constants
#define MQTT_KEEPALIVE_SECONDS 60
#define MQTT_PING_INTERVAL_US (MQTT_KEEPALIVE_SECONDS * 1000000 / 2)
#define MQTT_CONNECT_TIMEOUT_US 5000000  // 5 seconds

/* MQTT Client State */
typedef enum {
    MQ_STATE_IDLE,
    MQ_STATE_DNS,
    MQ_STATE_CONNECTING,
    MQ_STATE_CONNECTED,
    MQ_STATE_DISCONNECTING
} mq_state_t;

static struct {
    mq_state_t state;
    struct tcp_pcb *pcb;
    ip_addr_t broker_ip;
    uint16_t broker_port;
    char client_id[MQTT_CLIENT_ID_MAX];
    char username[MQTT_USERNAME_MAX];
    char password[MQTT_PASSWORD_MAX];
    bool use_auth;
    
    // Will/LWT
    char will_topic[MQTT_TOPIC_BUF_SIZE];
    char will_payload[MQTT_PAYLOAD_BUF_SIZE];
    uint16_t will_topic_len;
    uint16_t will_payload_len;
    uint8_t will_qos;
    bool will_retain;
    bool has_will;
    
    // Packet ID management
    uint16_t next_packet_id;
    
    // Keepalive
    absolute_time_t last_activity;
    absolute_time_t last_ping;
    
    // TX buffer
    uint8_t tx_buf[MQTT_TX_BUF_SIZE];
    uint16_t tx_buf_len;
    
    // RX buffer for incoming messages
    uint8_t rx_buf[MQTT_RX_BUF_SIZE];
    uint16_t rx_buf_len;
    uint16_t rx_buf_read;
    
    // Current received message
    char current_topic[MQTT_TOPIC_BUF_SIZE];
    uint16_t current_topic_len;
    char current_payload[MQTT_PAYLOAD_BUF_SIZE];
    uint16_t current_payload_len;
    bool message_available;
} mq;

/* Helper Functions */

static uint16_t mq_get_packet_id(void)
{
    if (++mq.next_packet_id == 0)
        mq.next_packet_id = 1;
    return mq.next_packet_id;
}

static void mq_reset(void)
{
    mq.state = MQ_STATE_IDLE;
    if (mq.pcb) {
        tcp_arg(mq.pcb, NULL);
        tcp_sent(mq.pcb, NULL);
        tcp_recv(mq.pcb, NULL);
        tcp_err(mq.pcb, NULL);
        tcp_close(mq.pcb);
        mq.pcb = NULL;
    }
    mq.tx_buf_len = 0;
    mq.rx_buf_len = 0;
    mq.rx_buf_read = 0;
    mq.message_available = false;
    mq.current_topic_len = 0;
    mq.current_payload_len = 0;
}

static void mq_update_activity(void)
{
    mq.last_activity = get_absolute_time();
}

/* MQTT Protocol Encoding/Decoding */

static uint16_t mq_encode_remaining_length(uint8_t *buf, uint32_t length)
{
    uint16_t pos = 0;
    do {
        uint8_t byte = length % 128;
        length /= 128;
        if (length > 0)
            byte |= 0x80;
        buf[pos++] = byte;
    } while (length > 0);
    return pos;
}

static uint32_t mq_decode_remaining_length(const uint8_t *buf, uint16_t *bytes_consumed)
{
    uint32_t multiplier = 1;
    uint32_t value = 0;
    uint8_t byte;
    uint16_t pos = 0;
    
    do {
        byte = buf[pos++];
        value += (byte & 127) * multiplier;
        multiplier *= 128;
        if (multiplier > 128 * 128 * 128)
            break;
    } while ((byte & 128) != 0);
    
    *bytes_consumed = pos;
    return value;
}

static uint16_t mq_encode_string(uint8_t *buf, const char *str, uint16_t len)
{
    buf[0] = (len >> 8) & 0xFF;
    buf[1] = len & 0xFF;
    memcpy(buf + 2, str, len);
    return len + 2;
}

// static uint16_t mq_decode_string(const uint8_t *buf, char *str, uint16_t max_len)
// {
//     uint16_t len = (buf[0] << 8) | buf[1];
//     if (len > max_len - 1)
//         len = max_len - 1;
//     memcpy(str, buf + 2, len);
//     str[len] = '\0';
//     return len;
// }

/* MQTT Packet Building */

static bool mq_build_connect(void)
{
    uint8_t *buf = mq.tx_buf;
    uint16_t pos = 0;
    
    // Fixed header
    buf[pos++] = (MQTT_MSG_TYPE_CONNECT << 4);
    
    // Calculate remaining length
    uint16_t remaining_len = 10; // Variable header
    remaining_len += 2 + strlen(mq.client_id);
    
    if (mq.has_will) {
        remaining_len += 2 + mq.will_topic_len;
        remaining_len += 2 + mq.will_payload_len;
    }
    
    if (mq.use_auth) {
        remaining_len += 2 + strlen(mq.username);
        remaining_len += 2 + strlen(mq.password);
    }
    
    pos += mq_encode_remaining_length(buf + pos, remaining_len);
    
    // Variable header - Protocol Name
    pos += mq_encode_string(buf + pos, "MQTT", 4);
    
    // Protocol Level (3.1.1 = 4)
    buf[pos++] = MQTT_PROTOCOL_VERSION;
    
    // Connect Flags
    uint8_t connect_flags = 0x02; // Clean Session
    if (mq.use_auth) {
        connect_flags |= 0x80; // Username flag
        connect_flags |= 0x40; // Password flag
    }
    if (mq.has_will) {
        connect_flags |= 0x04; // Will flag
        connect_flags |= (mq.will_qos & 0x03) << 3; // Will QoS
        if (mq.will_retain)
            connect_flags |= 0x20; // Will Retain
    }
    buf[pos++] = connect_flags;
    
    // Keep Alive
    buf[pos++] = (MQTT_KEEPALIVE_SECONDS >> 8) & 0xFF;
    buf[pos++] = MQTT_KEEPALIVE_SECONDS & 0xFF;
    
    // Payload - Client ID
    pos += mq_encode_string(buf + pos, mq.client_id, strlen(mq.client_id));
    
    // Will topic and message
    if (mq.has_will) {
        pos += mq_encode_string(buf + pos, mq.will_topic, mq.will_topic_len);
        pos += mq_encode_string(buf + pos, mq.will_payload, mq.will_payload_len);
    }
    
    // Username and password
    if (mq.use_auth) {
        pos += mq_encode_string(buf + pos, mq.username, strlen(mq.username));
        pos += mq_encode_string(buf + pos, mq.password, strlen(mq.password));
    }
    
    mq.tx_buf_len = pos;
    return true;
}

static bool mq_build_disconnect(void)
{
    uint8_t *buf = mq.tx_buf;
    buf[0] = (MQTT_MSG_TYPE_DISCONNECT << 4);
    buf[1] = 0; // Remaining length
    mq.tx_buf_len = 2;
    return true;
}

static bool mq_build_publish(const char *topic, uint16_t topic_len,
                             const uint8_t *payload, uint16_t payload_len,
                             uint8_t qos, bool retain)
{
    if (topic_len + payload_len + 10 > MQTT_TX_BUF_SIZE)
        return false;
    
    uint8_t *buf = mq.tx_buf;
    uint16_t pos = 0;
    
    // Fixed header
    uint8_t flags = (MQTT_MSG_TYPE_PUBLISH << 4);
    if (retain)
        flags |= 0x01;
    flags |= (qos & 0x03) << 1;
    buf[pos++] = flags;
    
    // Calculate remaining length
    uint16_t remaining_len = 2 + topic_len + payload_len;
    if (qos > 0)
        remaining_len += 2; // Packet ID
    
    pos += mq_encode_remaining_length(buf + pos, remaining_len);
    
    // Variable header - Topic
    pos += mq_encode_string(buf + pos, topic, topic_len);
    
    // Packet ID (for QoS > 0)
    if (qos > 0) {
        uint16_t packet_id = mq_get_packet_id();
        buf[pos++] = (packet_id >> 8) & 0xFF;
        buf[pos++] = packet_id & 0xFF;
    }
    
    // Payload
    memcpy(buf + pos, payload, payload_len);
    pos += payload_len;
    
    mq.tx_buf_len = pos;
    return true;
}

static bool mq_build_subscribe(const char *topic, uint16_t topic_len, uint8_t qos)
{
    if (topic_len + 10 > MQTT_TX_BUF_SIZE)
        return false;
    
    uint8_t *buf = mq.tx_buf;
    uint16_t pos = 0;
    
    // Fixed header
    buf[pos++] = (MQTT_MSG_TYPE_SUBSCRIBE << 4) | 0x02;
    
    // Calculate remaining length
    uint16_t remaining_len = 2 + 2 + topic_len + 1; // Packet ID + Topic + QoS
    pos += mq_encode_remaining_length(buf + pos, remaining_len);
    
    // Variable header - Packet ID
    uint16_t packet_id = mq_get_packet_id();
    buf[pos++] = (packet_id >> 8) & 0xFF;
    buf[pos++] = packet_id & 0xFF;
    
    // Payload - Topic filter
    pos += mq_encode_string(buf + pos, topic, topic_len);
    buf[pos++] = qos & 0x03;
    
    mq.tx_buf_len = pos;
    return true;
}

static bool mq_build_unsubscribe(const char *topic, uint16_t topic_len)
{
    if (topic_len + 10 > MQTT_TX_BUF_SIZE)
        return false;
    
    uint8_t *buf = mq.tx_buf;
    uint16_t pos = 0;
    
    // Fixed header
    buf[pos++] = (MQTT_MSG_TYPE_UNSUBSCRIBE << 4) | 0x02;
    
    // Calculate remaining length
    uint16_t remaining_len = 2 + 2 + topic_len; // Packet ID + Topic
    pos += mq_encode_remaining_length(buf + pos, remaining_len);
    
    // Variable header - Packet ID
    uint16_t packet_id = mq_get_packet_id();
    buf[pos++] = (packet_id >> 8) & 0xFF;
    buf[pos++] = packet_id & 0xFF;
    
    // Payload - Topic filter
    pos += mq_encode_string(buf + pos, topic, topic_len);
    
    mq.tx_buf_len = pos;
    return true;
}

static bool mq_build_pingreq(void)
{
    uint8_t *buf = mq.tx_buf;
    buf[0] = (MQTT_MSG_TYPE_PINGREQ << 4);
    buf[1] = 0; // Remaining length
    mq.tx_buf_len = 2;
    return true;
}

/* MQTT Packet Parsing */

static void mq_handle_connack(const uint8_t *buf, uint16_t len)
{
    if (len < 2) {
        DBG("MQTT: CONNACK too short\n");
        mq_reset();
        return;
    }
    
    uint8_t return_code = buf[1];
    if (return_code == 0) {
        DBG("MQTT: Connected\n");
        mq.state = MQ_STATE_CONNECTED;
        mq_update_activity();
    } else {
        DBG("MQTT: Connection refused, code %d\n", return_code);
        mq_reset();
    }
}

static void mq_handle_publish(const uint8_t *buf, uint16_t len)
{
    if (mq.message_available) {
        DBG("MQTT: Message overflow, dropping\n");
        return;
    }
    
    uint16_t pos = 0;
    
    // Decode topic
    uint16_t topic_len = (buf[pos] << 8) | buf[pos + 1];
    pos += 2;
    
    if (topic_len >= MQTT_TOPIC_BUF_SIZE - 1)
        topic_len = MQTT_TOPIC_BUF_SIZE - 1;
    
    memcpy(mq.current_topic, buf + pos, topic_len);
    mq.current_topic[topic_len] = '\0';
    mq.current_topic_len = topic_len;
    pos += topic_len;
    
    // Skip packet ID if QoS > 0 (we can determine from flags)
    // For simplicity, we'll just copy the payload
    
    // Payload is the rest
    uint16_t payload_len = len - pos;
    if (payload_len >= MQTT_PAYLOAD_BUF_SIZE - 1)
        payload_len = MQTT_PAYLOAD_BUF_SIZE - 1;
    
    memcpy(mq.current_payload, buf + pos, payload_len);
    mq.current_payload[payload_len] = '\0';
    mq.current_payload_len = payload_len;
    
    mq.message_available = true;
    mq_update_activity();
    
    DBG("MQTT: Received message on '%s'\n", mq.current_topic);
}

static void mq_handle_pingresp(void)
{
    DBG("MQTT: PINGRESP received\n");
    mq_update_activity();
}

static void mq_parse_packet(const uint8_t *buf, uint16_t len)
{
    if (len < 2)
        return;
    
    uint8_t msg_type = (buf[0] >> 4) & 0x0F;
    uint16_t bytes_consumed;
    uint32_t remaining_len = mq_decode_remaining_length(buf + 1, &bytes_consumed);
    
    const uint8_t *payload = buf + 1 + bytes_consumed;
    
    switch (msg_type) {
    case MQTT_MSG_TYPE_CONNACK:
        mq_handle_connack(payload, remaining_len);
        break;
    case MQTT_MSG_TYPE_PUBLISH:
        mq_handle_publish(payload, remaining_len);
        break;
    case MQTT_MSG_TYPE_PUBACK:
    case MQTT_MSG_TYPE_SUBACK:
    case MQTT_MSG_TYPE_UNSUBACK:
        mq_update_activity();
        break;
    case MQTT_MSG_TYPE_PINGRESP:
        mq_handle_pingresp();
        break;
    default:
        DBG("MQTT: Unknown message type %d\n", msg_type);
        break;
    }
}

/* lwIP TCP Callbacks */

static err_t mq_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    (void)arg;
    
    if (err != ERR_OK || p == NULL) {
        if (p)
            pbuf_free(p);
        mq_reset();
        return ERR_OK;
    }
    
    // Copy data to RX buffer
    uint16_t copy_len = p->tot_len;
    if (mq.rx_buf_len + copy_len > MQTT_RX_BUF_SIZE) {
        DBG("MQTT: RX buffer overflow\n");
        pbuf_free(p);
        tcp_recved(tpcb, p->tot_len);
        return ERR_OK;
    }
    
    pbuf_copy_partial(p, mq.rx_buf + mq.rx_buf_len, copy_len, 0);
    mq.rx_buf_len += copy_len;
    
    pbuf_free(p);
    tcp_recved(tpcb, copy_len);
    
    // Try to parse complete packets
    while (mq.rx_buf_read < mq.rx_buf_len) {
        if (mq.rx_buf_len - mq.rx_buf_read < 2)
            break;
        
        uint16_t bytes_consumed;
        uint32_t remaining_len = mq_decode_remaining_length(
            mq.rx_buf + mq.rx_buf_read + 1, &bytes_consumed);
        
        uint16_t packet_len = 1 + bytes_consumed + remaining_len;
        
        if (mq.rx_buf_read + packet_len > mq.rx_buf_len)
            break; // Incomplete packet
        
        mq_parse_packet(mq.rx_buf + mq.rx_buf_read, packet_len);
        mq.rx_buf_read += packet_len;
    }
    
    // Compact buffer
    if (mq.rx_buf_read > 0) {
        if (mq.rx_buf_read < mq.rx_buf_len) {
            memmove(mq.rx_buf, mq.rx_buf + mq.rx_buf_read, 
                   mq.rx_buf_len - mq.rx_buf_read);
            mq.rx_buf_len -= mq.rx_buf_read;
        } else {
            mq.rx_buf_len = 0;
        }
        mq.rx_buf_read = 0;
    }
    
    return ERR_OK;
}

static err_t mq_tcp_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    (void)arg;
    (void)tpcb;
    (void)len;
    mq_update_activity();
    return ERR_OK;
}

static void mq_tcp_err(void *arg, err_t err)
{
    (void)arg;
    DBG("MQTT: TCP error %d\n", err);
    mq_reset();
}

static err_t mq_tcp_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    (void)arg;
    
    if (err != ERR_OK) {
        DBG("MQTT: Connection failed %d\n", err);
        mq_reset();
        return err;
    }
    
    DBG("MQTT: TCP connected\n");
    
    tcp_recv(tpcb, mq_tcp_recv);
    tcp_sent(tpcb, mq_tcp_sent);
    tcp_err(tpcb, mq_tcp_err);
    
    // Send CONNECT packet
    if (!mq_build_connect()) {
        mq_reset();
        return ERR_MEM;
    }
    
    err = tcp_write(tpcb, mq.tx_buf, mq.tx_buf_len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(tpcb);
        mq.state = MQ_STATE_CONNECTING;
        mq_update_activity();
    } else {
        DBG("MQTT: Failed to send CONNECT %d\n", err);
        mq_reset();
    }
    
    return ERR_OK;
}

static void mq_dns_found(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name;
    (void)arg;
    
    if (ipaddr == NULL) {
        DBG("MQTT: DNS lookup failed\n");
        mq_reset();
        return;
    }
    
    DBG("MQTT: DNS resolved\n");
    mq.broker_ip = *ipaddr;
    
    // Create TCP connection
    mq.pcb = tcp_new();
    if (!mq.pcb) {
        DBG("MQTT: Failed to create PCB\n");
        mq_reset();
        return;
    }
    
    tcp_arg(mq.pcb, NULL);
    
    err_t err = tcp_connect(mq.pcb, &mq.broker_ip, mq.broker_port, mq_tcp_connected);
    if (err != ERR_OK) {
        DBG("MQTT: TCP connect failed %d\n", err);
        mq_reset();
    }
}

/* Public API */

void mq_init(void)
{
    memset(&mq, 0, sizeof(mq));
    mq.state = MQ_STATE_IDLE;
    mq.next_packet_id = 1;
    strcpy(mq.client_id, "rp6502");
}

void mq_task(void)
{
    if (mq.state == MQ_STATE_CONNECTED) {
        // Send periodic PING
        absolute_time_t now = get_absolute_time();
        if (absolute_time_diff_us(mq.last_ping, now) > MQTT_PING_INTERVAL_US) {
            if (mq_build_pingreq()) {
                if (tcp_write(mq.pcb, mq.tx_buf, mq.tx_buf_len, TCP_WRITE_FLAG_COPY) == ERR_OK) {
                    tcp_output(mq.pcb);
                    mq.last_ping = now;
                }
            }
        }
    }
}

void mq_stop(void)
{
    if (mq.state != MQ_STATE_IDLE) {
        if (mq.state == MQ_STATE_CONNECTED) {
            mq_build_disconnect();
            tcp_write(mq.pcb, mq.tx_buf, mq.tx_buf_len, TCP_WRITE_FLAG_COPY);
            tcp_output(mq.pcb);
        }
        mq_reset();
    }
}

/* API Implementations */

bool mq_api_connect(void)
{
    if (mq.state != MQ_STATE_IDLE)
        return api_return_errno(API_EBUSY);
    
    // Get hostname from XRAM (passed in A/X)
    uint32_t hostname_addr = API_AX;
    if (hostname_addr >= XRAM_SIZE)
        return api_return_errno(API_EINVAL);
    
    char hostname[256];
    size_t i;

    DBG("MQTT: Reading hostname from xram[0x%04lx]:\n", (unsigned long)hostname_addr);
    for (i = 0; i < sizeof(hostname) - 1 && hostname_addr + i < XRAM_SIZE; i++) {
        hostname[i] = xram[hostname_addr + i];
        DBG("  [%zu] = 0x%02x '%c'\n", i, (unsigned char)hostname[i], 
            hostname[i] >= 32 && hostname[i] < 127 ? hostname[i] : '.');
        if (hostname[i] == 0)
            break;
    }
    hostname[i] = '\0';
    DBG("MQTT: Hostname read: '%s' (length %zu)\n", hostname, i);
    
    // Pop client_id from stack first
    uint16_t client_id_addr16;
    if (!api_pop_uint16(&client_id_addr16))
        return api_return_errno(API_EINVAL);

    uint32_t client_id_addr = client_id_addr16;
    if (client_id_addr >= XRAM_SIZE)
        return api_return_errno(API_EINVAL);
    
    for (i = 0; i < sizeof(mq.client_id) - 1 && client_id_addr + i < XRAM_SIZE; i++) {
        mq.client_id[i] = xram[client_id_addr + i];
        if (mq.client_id[i] == 0)
            break;
    }
    mq.client_id[i] = '\0';
    
    if (strlen(mq.client_id) == 0)
        strcpy(mq.client_id, "rp6502");
    
    // Pop port last with _end variant
    uint16_t port;
    if (!api_pop_uint16_end(&port))
        return api_return_errno(API_EINVAL);
    
    DBG("MQTT: Connecting to %s:%d as %s\n", hostname, port, mq.client_id);
    
    mq.broker_port = port;
    mq.state = MQ_STATE_DNS;
    mq.last_activity = get_absolute_time();
    mq.last_ping = mq.last_activity;
    
    // Resolve hostname
    err_t err = dns_gethostbyname(hostname, &mq.broker_ip, mq_dns_found, NULL);
    if (err == ERR_OK) {
        // Already resolved
        mq_dns_found(hostname, &mq.broker_ip, NULL);
    } else if (err != ERR_INPROGRESS) {
        DBG("MQTT: DNS query failed %d\n", err);
        mq_reset();
        return api_return_errno(API_EIO);
    }
    
    return api_return_ax(0);
}

bool mq_api_disconnect(void)
{
    if (mq.state != MQ_STATE_CONNECTED)
        return api_return_errno(API_EINVAL);
    
    mq_build_disconnect();
    tcp_write(mq.pcb, mq.tx_buf, mq.tx_buf_len, TCP_WRITE_FLAG_COPY);
    tcp_output(mq.pcb);
    mq_reset();
    
    return api_return_ax(0);
}

bool mq_api_publish(void)
{
    if (mq.state != MQ_STATE_CONNECTED)
        return api_return_errno(API_EINVAL);
    
    // Reset publish_done flag before attempting publish
    API_MQ_PUBLISH_DONE = 0;
    
    uint8_t qos, retain;
    uint16_t topic_len, payload_len;
    uint16_t topic_addr, payload_addr;
    
    // Pop parameters
    if (!api_pop_uint8(&qos))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint8(&retain))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16(&topic_len))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16(&topic_addr))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16(&payload_len))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16_end(&payload_addr))
        return api_return_errno(API_EINVAL);
    
    if (topic_addr + topic_len > XRAM_SIZE || 
        payload_addr + payload_len > XRAM_SIZE)
        return api_return_errno(API_EINVAL);
    
    if (!mq_build_publish((char *)(xram + topic_addr), topic_len,
                         xram + payload_addr, payload_len,
                         qos & 0x03, retain != 0))
        return api_return_errno(API_ENOMEM);
    
    err_t err = tcp_write(mq.pcb, mq.tx_buf, mq.tx_buf_len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(mq.pcb);
        mq_update_activity();
        API_MQ_PUBLISH_DONE = 1;
        return api_return_ax(0);
    }
    
    return api_return_errno(API_EIO);
}

bool mq_api_subscribe(void)
{
    if (mq.state != MQ_STATE_CONNECTED)
        return api_return_errno(API_EINVAL);
    
    uint8_t qos;
    uint16_t topic_len, topic_addr;
    
    if (!api_pop_uint8(&qos))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16(&topic_len))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16_end(&topic_addr))
        return api_return_errno(API_EINVAL);
    
    if (topic_addr + topic_len > XRAM_SIZE)
        return api_return_errno(API_EINVAL);
    
    if (!mq_build_subscribe((char *)(xram + topic_addr), topic_len, qos & 0x03))
        return api_return_errno(API_ENOMEM);
    
    err_t err = tcp_write(mq.pcb, mq.tx_buf, mq.tx_buf_len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(mq.pcb);
        mq_update_activity();
        return api_return_ax(0);
    }
    
    return api_return_errno(API_EIO);
}

bool mq_api_unsubscribe(void)
{
    if (mq.state != MQ_STATE_CONNECTED)
        return api_return_errno(API_EINVAL);
    
    uint16_t topic_len, topic_addr;
    
    if (!api_pop_uint16(&topic_len))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16_end(&topic_addr))
        return api_return_errno(API_EINVAL);
    
    if (topic_addr + topic_len > XRAM_SIZE)
        return api_return_errno(API_EINVAL);
    
    if (!mq_build_unsubscribe((char *)(xram + topic_addr), topic_len))
        return api_return_errno(API_ENOMEM);
    
    err_t err = tcp_write(mq.pcb, mq.tx_buf, mq.tx_buf_len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(mq.pcb);
        mq_update_activity();
        return api_return_ax(0);
    }
    
    return api_return_errno(API_EIO);
}

bool mq_api_poll(void)
{
    return api_return_ax(mq.message_available ? mq.current_payload_len : 0);
}

bool mq_api_read_message(void)
{
    if (!mq.message_available)
        return api_return_ax(0);
    
    uint16_t buf_len, buf_addr;
    
    if (!api_pop_uint16(&buf_len))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16_end(&buf_addr))
        return api_return_errno(API_EINVAL);
    
    if (buf_addr + buf_len > XRAM_SIZE)
        return api_return_errno(API_EINVAL);
    
    uint16_t copy_len = mq.current_payload_len;
    if (copy_len > buf_len)
        copy_len = buf_len;
    
    memcpy(xram + buf_addr, mq.current_payload, copy_len);
    mq.message_available = false;
    
    return api_return_ax(copy_len);
}

bool mq_api_get_topic(void)
{
    if (!mq.current_topic_len)
        return api_return_ax(0);
    
    uint16_t buf_len, buf_addr;
    
    if (!api_pop_uint16(&buf_len))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16_end(&buf_addr))
        return api_return_errno(API_EINVAL);
    
    if (buf_addr + buf_len > XRAM_SIZE)
        return api_return_errno(API_EINVAL);
    
    uint16_t copy_len = mq.current_topic_len;
    if (copy_len > buf_len)
        copy_len = buf_len;
    
    memcpy(xram + buf_addr, mq.current_topic, copy_len);
    if (copy_len < buf_len)
        xram[buf_addr + copy_len] = '\0';
    
    return api_return_ax(copy_len);
}

bool mq_api_connected(void)
{
    return api_return_ax(mq.state == MQ_STATE_CONNECTED ? 1 : 0);
}

bool mq_api_set_auth(void)
{
    uint16_t password_len, username_len;
    uint16_t password_addr, username_addr;
    
    if (!api_pop_uint16(&password_len))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16(&password_addr))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16(&username_len))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16_end(&username_addr))
        return api_return_errno(API_EINVAL);
    
    if (username_addr + username_len > XRAM_SIZE ||
        password_addr + password_len > XRAM_SIZE)
        return api_return_errno(API_EINVAL);
    
    if (username_len >= MQTT_USERNAME_MAX)
        username_len = MQTT_USERNAME_MAX - 1;
    if (password_len >= MQTT_PASSWORD_MAX)
        password_len = MQTT_PASSWORD_MAX - 1;
    
    memcpy(mq.username, xram + username_addr, username_len);
    mq.username[username_len] = '\0';
    
    memcpy(mq.password, xram + password_addr, password_len);
    mq.password[password_len] = '\0';
    
    mq.use_auth = (username_len > 0);
    
    return api_return_ax(0);
}

bool mq_api_set_will(void)
{
    uint8_t will_qos, will_retain;
    uint16_t will_topic_len, will_payload_len;
    uint16_t will_topic_addr, will_payload_addr;
    
    if (!api_pop_uint8(&will_qos))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint8(&will_retain))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16(&will_topic_len))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16(&will_topic_addr))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16(&will_payload_len))
        return api_return_errno(API_EINVAL);
    if (!api_pop_uint16_end(&will_payload_addr))
        return api_return_errno(API_EINVAL);
    
    if (will_topic_addr + will_topic_len > XRAM_SIZE ||
        will_payload_addr + will_payload_len > XRAM_SIZE)
        return api_return_errno(API_EINVAL);
    
    if (will_topic_len >= MQTT_TOPIC_BUF_SIZE)
        will_topic_len = MQTT_TOPIC_BUF_SIZE - 1;
    if (will_payload_len >= MQTT_PAYLOAD_BUF_SIZE)
        will_payload_len = MQTT_PAYLOAD_BUF_SIZE - 1;
    
    memcpy(mq.will_topic, xram + will_topic_addr, will_topic_len);
    mq.will_topic[will_topic_len] = '\0';
    mq.will_topic_len = will_topic_len;
    
    memcpy(mq.will_payload, xram + will_payload_addr, will_payload_len);
    mq.will_payload[will_payload_len] = '\0';
    mq.will_payload_len = will_payload_len;
    
    mq.will_qos = will_qos & 0x03;
    mq.will_retain = (will_retain != 0);
    mq.has_will = true;
    
    return api_return_ax(0);
}

#endif /* RP6502_RIA_W */
