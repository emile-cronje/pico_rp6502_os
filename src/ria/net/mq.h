/*
 * Copyright (c) 2025 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _RIA_NET_MQ_H_
#define _RIA_NET_MQ_H_

/* MQTT client driver
 * Provides MQTT 3.1.1 client functionality for 6502 applications
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "lwip/err.h"

/* Main events
 */

void mq_init(void);
void mq_task(void);
void mq_stop(void);

/* API operations
 */

// Connect to MQTT broker
// Stack: uint16_t port, uint8_t *hostname, uint8_t *client_id
// Returns: 0 on success, errno on error
bool mq_api_connect(void);

// Disconnect from MQTT broker
// Returns: 0 on success
bool mq_api_disconnect(void);

// Publish message
// Stack: uint8_t qos, uint8_t retain, uint16_t topic_len, uint8_t *topic, uint16_t payload_len, uint8_t *payload
// Returns: 0 on success, errno on error
bool mq_api_publish(void);

// Subscribe to topic
// Stack: uint8_t qos, uint16_t topic_len, uint8_t *topic
// Returns: 0 on success, errno on error
bool mq_api_subscribe(void);

// Unsubscribe from topic
// Stack: uint16_t topic_len, uint8_t *topic
// Returns: 0 on success, errno on error
bool mq_api_unsubscribe(void);

// Poll for incoming messages
// Returns: number of bytes available, 0 if no message
bool mq_api_poll(void);

// Read incoming message
// Stack: uint16_t buf_len, uint8_t *buffer
// Returns: number of bytes read, or errno on error
bool mq_api_read_message(void);

// Get last published message topic
// Stack: uint16_t buf_len, uint8_t *buffer
// Returns: number of bytes written, or errno on error
bool mq_api_get_topic(void);

// Check connection status
// Returns: 1 if connected, 0 if not connected
bool mq_api_connected(void);

// Set authentication credentials
// Stack: uint16_t password_len, uint8_t *password, uint16_t username_len, uint8_t *username
// Returns: 0 on success
bool mq_api_set_auth(void);

// Set will message (LWT - Last Will and Testament)
// Stack: uint8_t will_qos, uint8_t will_retain, uint16_t will_topic_len, uint8_t *will_topic, uint16_t will_payload_len, uint8_t *will_payload
// Returns: 0 on success
bool mq_api_set_will(void);

#endif /* _RIA_NET_MQ_H_ */
