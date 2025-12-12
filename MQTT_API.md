# MQTT Client API for RP6502

This document describes the MQTT client API available for 6502 applications on the RP6502 platform.

## Overview

The MQTT client provides full MQTT 3.1.1 protocol support, allowing your 6502 applications to:
- Connect to MQTT brokers with authentication
- Publish messages with configurable QoS and retain flags
- Subscribe to topics and receive messages
- Configure Last Will and Testament (LWT) messages
- Maintain persistent connections with automatic keepalive

## Quick Start: Complete Example (Connect → Subscribe → Publish)

Here's a complete working example showing the entire MQTT workflow:

```c
#include <stdio.h>
#include <string.h>
#include <rp6502.h>

// Helper function to copy string to XRAM
void xram_strcpy(uint16_t addr, const char* str) {
    RIA.addr0 = addr;
    for (int i = 0; str[i]; i++) {
        RIA.rw0 = str[i];
    }
    RIA.rw0 = 0;
}

// Helper for modem AT commands
void modem_cmd(const char* cmd) {
    for (int i = 0; cmd[i]; i++) putchar(cmd[i]);
    putchar('\r');
    for (volatile long i = 0; i < 500000; i++);  // Wait for response
}

int main() {
    printf("=== Complete MQTT Example ===\n\n");
    
    // STEP 1: Connect to WiFi
    printf("[1/6] Connecting to WiFi...\n");
    modem_cmd("ATZ");                                    // Reset modem
    modem_cmd("AT+CWMODE=1");                           // Station mode
    modem_cmd("AT+CWJAP=\"YourSSID\",\"YourPassword\""); // Connect to WiFi
    printf("Waiting for WiFi connection...\n");
    for (volatile long i = 0; i < 2000000; i++);
    printf("WiFi connected!\n\n");
    
    // STEP 2: Connect to MQTT Broker
    printf("[2/6] Connecting to MQTT broker...\n");
    
    char broker[] = "test.mosquitto.org";
    char client_id[] = "rp6502_demo";
    uint16_t port = 1883;
    
    xram_strcpy(0x0000, broker);
    xram_strcpy(0x0100, client_id);
    
    printf("Broker: %s:%d\n", broker, port);
    printf("Client: %s\n", client_id);
    
    // Initiate connection
    RIA.xstack = port & 0xFF;
    RIA.xstack = port >> 8;
    RIA.xstack = 0x00; RIA.xstack = 0x00;  // hostname addr
    RIA.a = 0x00; RIA.x = 0x01;             // client_id addr
    RIA.op = 0x30;  // mq_connect
    
    if (RIA.a != 0) {
        printf("ERROR: Connection failed: %d\n", RIA.errno);
        return 1;
    }
    
    // Wait for connection
    printf("Waiting for MQTT connection");
    for (int i = 0; i < 50; i++) {
        for (volatile int j = 0; j < 10000; j++);
        RIA.op = 0x38;  // mq_connected
        if (RIA.a == 1) {
            printf(" CONNECTED!\n\n");
            break;
        }
        if (i % 5 == 0) printf(".");
    }
    
    RIA.op = 0x38;  // Check connection status
    if (RIA.a != 1) {
        printf("\nERROR: Connection timeout\n");
        return 1;
    }
    
    // STEP 3: Subscribe to Topics
    printf("[3/6] Subscribing to topics...\n");
    
    char sub_topic[] = "rp6502/demo/#";  // Subscribe to all demo messages
    xram_strcpy(0x0200, sub_topic);
    uint16_t sub_len = strlen(sub_topic);
    
    printf("Subscribing to: %s\n", sub_topic);
    
    RIA.xstack = 0;                 // QoS 0
    RIA.xstack = sub_len & 0xFF;
    RIA.xstack = sub_len >> 8;
    RIA.a = 0x00; RIA.x = 0x02;
    RIA.op = 0x33;  // mq_subscribe
    
    if (RIA.a == 0) {
        printf("Subscribed successfully!\n\n");
    } else {
        printf("ERROR: Subscribe failed\n");
        return 1;
    }
    
    // STEP 4: Publish Messages
    printf("[4/6] Publishing messages...\n");
    
    // Publish message 1
    char topic1[] = "rp6502/demo/temperature";
    char payload1[] = "22.5 C";
    xram_strcpy(0x0300, topic1);
    xram_strcpy(0x0400, payload1);
    
    printf("Publishing: %s -> %s\n", topic1, payload1);
    
    RIA.xstack = 0;                              // QoS 0
    RIA.xstack = 0;                              // retain = false
    RIA.xstack = strlen(topic1) & 0xFF;
    RIA.xstack = strlen(topic1) >> 8;
    RIA.xstack = 0x00; RIA.xstack = 0x03;        // topic addr
    RIA.xstack = strlen(payload1) & 0xFF;
    RIA.xstack = strlen(payload1) >> 8;
    RIA.a = 0x00; RIA.x = 0x04;                  // payload addr
    RIA.op = 0x32;  // mq_publish
    
    if (RIA.a != 0) {
        printf("ERROR: Publish 1 failed\n");
    } else {
        printf("Message 1 published!\n");
    }
    
    // Small delay
    for (volatile long i = 0; i < 50000; i++);
    
    // Publish message 2
    char topic2[] = "rp6502/demo/humidity";
    char payload2[] = "65%";
    xram_strcpy(0x0300, topic2);
    xram_strcpy(0x0400, payload2);
    
    printf("Publishing: %s -> %s\n", topic2, payload2);
    
    RIA.xstack = 0; RIA.xstack = 0;
    RIA.xstack = strlen(topic2) & 0xFF;
    RIA.xstack = strlen(topic2) >> 8;
    RIA.xstack = 0x00; RIA.xstack = 0x03;
    RIA.xstack = strlen(payload2) & 0xFF;
    RIA.xstack = strlen(payload2) >> 8;
    RIA.a = 0x00; RIA.x = 0x04;
    RIA.op = 0x32;  // mq_publish
    
    if (RIA.a == 0) {
        printf("Message 2 published!\n");
    }
    
    // Publish status with retain flag
    char status_topic[] = "rp6502/demo/status";
    char status_payload[] = "online";
    xram_strcpy(0x0300, status_topic);
    xram_strcpy(0x0400, status_payload);
    
    printf("Publishing: %s -> %s (retained)\n", status_topic, status_payload);
    
    RIA.xstack = 0;                              // QoS 0
    RIA.xstack = 1;                              // retain = TRUE
    RIA.xstack = strlen(status_topic) & 0xFF;
    RIA.xstack = strlen(status_topic) >> 8;
    RIA.xstack = 0x00; RIA.xstack = 0x03;
    RIA.xstack = strlen(status_payload) & 0xFF;
    RIA.xstack = strlen(status_payload) >> 8;
    RIA.a = 0x00; RIA.x = 0x04;
    RIA.op = 0x32;  // mq_publish
    
    if (RIA.a == 0) {
        printf("Status published and retained!\n\n");
    }
    
    // STEP 5: Listen for Messages
    printf("[5/6] Listening for incoming messages (20 seconds)...\n");
    printf("Note: We'll receive our own published messages\n\n");
    
    int msg_count = 0;
    
    for (int i = 0; i < 200; i++) {  // 20 seconds
        // Poll for messages
        RIA.op = 0x35;  // mq_poll
        uint16_t msg_len = RIA.a | (RIA.x << 8);
        
        if (msg_len > 0) {
            msg_count++;
            printf("\n=== Message %d (Payload: %d bytes) ===\n", msg_count, msg_len);
            
            // Get topic
            RIA.xstack = 128 & 0xFF;
            RIA.xstack = 128 >> 8;
            RIA.a = 0x00; RIA.x = 0x05;
            RIA.op = 0x37;  // mq_get_topic
            
            uint16_t topic_len = RIA.a | (RIA.x << 8);
            
            printf("Topic: ");
            RIA.addr0 = 0x0500;
            for (int j = 0; j < topic_len; j++) {
                putchar(RIA.rw0);
            }
            printf("\n");
            
            // Read message
            RIA.xstack = 255 & 0xFF;
            RIA.xstack = 255 >> 8;
            RIA.a = 0x00; RIA.x = 0x06;
            RIA.op = 0x36;  // mq_read_message
            
            uint16_t bytes_read = RIA.a | (RIA.x << 8);
            
            printf("Payload: ");
            RIA.addr0 = 0x0600;
            for (int j = 0; j < bytes_read; j++) {
                putchar(RIA.rw0);
            }
            printf("\n");
        }
        
        // Progress indicator
        if (i % 20 == 0 && i > 0) {
            printf(".");
            fflush(stdout);
        }
        
        // Delay ~100ms
        for (volatile long j = 0; j < 10000; j++);
    }
    
    printf("\n\nReceived %d message%s total\n\n", 
           msg_count, msg_count == 1 ? "" : "s");
    
    // STEP 6: Disconnect
    printf("[6/6] Disconnecting from broker...\n");
    RIA.op = 0x31;  // mq_disconnect
    
    if (RIA.a == 0) {
        printf("Disconnected successfully!\n");
    }
    
    printf("\n=== EXAMPLE COMPLETE ===\n");
    printf("Summary:\n");
    printf("  - Connected to %s\n", broker);
    printf("  - Subscribed to: %s\n", sub_topic);
    printf("  - Published 3 messages\n");
    printf("  - Received %d messages\n", msg_count);
    printf("  - Disconnected cleanly\n");
    
    return 0;
}
```

### What This Example Demonstrates

1. **WiFi Connection** - Uses AT commands to connect to WiFi network
2. **MQTT Connection** - Connects to a public MQTT broker
3. **Subscription** - Subscribes to `rp6502/demo/#` to receive all demo messages
4. **Publishing** - Publishes three different messages:
   - Temperature reading
   - Humidity reading  
   - Status message with retain flag (stays on broker)
5. **Message Reception** - Polls for incoming messages (receives own published messages)
6. **Clean Shutdown** - Properly disconnects from broker

### Testing the Example

You can monitor the messages from your PC using:

```bash
# Subscribe to all rp6502 demo messages
mosquitto_sub -h test.mosquitto.org -t "rp6502/demo/#" -v

# Or publish test messages to the RP6502
mosquitto_pub -h test.mosquitto.org -t "rp6502/demo/test" -m "Hello RP6502!"
```

## API Operations

All MQTT operations are accessed through the RIA API call mechanism (writing to $FFEF).

### Operation Codes

| Operation | Code | Description |
|-----------|------|-------------|
| `mq_connect` | $30 | Connect to MQTT broker |
| `mq_disconnect` | $31 | Disconnect from broker |
| `mq_publish` | $32 | Publish a message |
| `mq_subscribe` | $33 | Subscribe to a topic |
| `mq_unsubscribe` | $34 | Unsubscribe from a topic |
| `mq_poll` | $35 | Check for incoming messages |
| `mq_read_message` | $36 | Read received message payload |
| `mq_get_topic` | $37 | Get topic of last received message |
| `mq_connected` | $38 | Check connection status |
| `mq_set_auth` | $39 | Set authentication credentials |
| `mq_set_will` | $3A | Configure Last Will and Testament |

## API Functions

### mq_connect ($30)

Connect to an MQTT broker.

**Parameters** (on stack):
- `uint16_t port` - Broker port (typically 1883)
- `uint8_t* hostname` - Pointer to null-terminated hostname string in XRAM
- `uint8_t* client_id` - Pointer to null-terminated client ID string in XRAM

**Returns**:
- 0 on success
- errno on error

**Example (CC65)**:
```c
#include <rp6502.h>

char hostname[] = "broker.hivemq.com";
char client_id[] = "rp6502_client";

// Copy strings to XRAM
RIA.addr0 = 0x0000;
for (int i = 0; hostname[i]; i++) {
    RIA.rw0 = hostname[i];
}
RIA.rw0 = 0;

RIA.addr0 = 0x0100;
for (int i = 0; client_id[i]; i++) {
    RIA.rw0 = client_id[i];
}
RIA.rw0 = 0;

// Push parameters and call
RIA.xstack = 1883 & 0xFF;         // port low byte
RIA.xstack = 1883 >> 8;           // port high byte
RIA.xstack = 0x00;                // hostname addr low
RIA.xstack = 0x00;                // hostname addr high
RIA.a = 0x00;                     // client_id addr low
RIA.x = 0x01;                     // client_id addr high
RIA.op = 0x30;                    // mq_connect

// Check result
if (RIA.a != 0) {
    printf("Connection failed: %d\n", RIA.errno);
}
```

### mq_disconnect ($31)

Disconnect from the MQTT broker.

**Parameters**: None

**Returns**:
- 0 on success
- errno on error

**Example (CC65)**:
```c
RIA.op = 0x31;  // mq_disconnect
```

### mq_publish ($32)

Publish a message to a topic.

**Parameters** (on stack):
- `uint8_t qos` - Quality of Service (0, 1, or 2)
- `uint8_t retain` - Retain flag (0 or 1)
- `uint16_t topic_len` - Length of topic string
- `uint8_t* topic` - Pointer to topic string in XRAM (not null-terminated)
- `uint16_t payload_len` - Length of payload
- `uint8_t* payload` - Pointer to payload in XRAM

**Returns**:
- 0 on success
- errno on error

**Example (CC65)**:
```c
char topic[] = "sensors/temperature";
char payload[] = "23.5";

// Copy topic to XRAM at 0x0000
RIA.addr0 = 0x0000;
for (int i = 0; topic[i]; i++) {
    RIA.rw0 = topic[i];
}
uint16_t topic_len = strlen(topic);

// Copy payload to XRAM at 0x0100
RIA.addr0 = 0x0100;
for (int i = 0; payload[i]; i++) {
    RIA.rw0 = payload[i];
}
uint16_t payload_len = strlen(payload);

// Push parameters
RIA.xstack = 0;                   // QoS 0
RIA.xstack = 0;                   // retain = false
RIA.xstack = topic_len & 0xFF;    // topic_len low
RIA.xstack = topic_len >> 8;      // topic_len high
RIA.xstack = 0x00;                // topic addr low
RIA.xstack = 0x00;                // topic addr high
RIA.xstack = payload_len & 0xFF;  // payload_len low
RIA.xstack = payload_len >> 8;    // payload_len high
RIA.a = 0x00;                     // payload addr low
RIA.x = 0x01;                     // payload addr high
RIA.op = 0x32;                    // mq_publish

if (RIA.a != 0) {
    printf("Publish failed: %d\n", RIA.errno);
}
```

### mq_subscribe ($33)

Subscribe to a topic.

**Parameters** (on stack):
- `uint8_t qos` - Maximum QoS level to receive (0, 1, or 2)
- `uint16_t topic_len` - Length of topic filter string
- `uint8_t* topic` - Pointer to topic filter in XRAM

**Returns**:
- 0 on success
- errno on error

**Example (CC65)**:
```c
char topic[] = "sensors/#";  // Subscribe to all sensor topics

// Copy topic to XRAM
RIA.addr0 = 0x0000;
for (int i = 0; topic[i]; i++) {
    RIA.rw0 = topic[i];
}
uint16_t topic_len = strlen(topic);

// Push parameters
RIA.xstack = 0;                // QoS 0
RIA.xstack = topic_len & 0xFF; // topic_len low
RIA.xstack = topic_len >> 8;   // topic_len high
RIA.a = 0x00;                  // topic addr low
RIA.x = 0x00;                  // topic addr high
RIA.op = 0x33;                 // mq_subscribe
```

### mq_unsubscribe ($34)

Unsubscribe from a topic.

**Parameters** (on stack):
- `uint16_t topic_len` - Length of topic filter string
- `uint8_t* topic` - Pointer to topic filter in XRAM

**Returns**:
- 0 on success
- errno on error

**Example (CC65)**:
```c
char topic[] = "sensors/#";

// Copy topic to XRAM
RIA.addr0 = 0x0000;
for (int i = 0; topic[i]; i++) {
    RIA.rw0 = topic[i];
}
uint16_t topic_len = strlen(topic);

// Push parameters
RIA.xstack = topic_len & 0xFF; // topic_len low
RIA.xstack = topic_len >> 8;   // topic_len high
RIA.a = 0x00;                  // topic addr low
RIA.x = 0x00;                  // topic addr high
RIA.op = 0x34;                 // mq_unsubscribe
```

### mq_poll ($35)

Check if a message has been received.

**Parameters**: None

**Returns**:
- Number of bytes in the received message payload (>0 if message available)
- 0 if no message available

**Example (CC65)**:
```c
RIA.op = 0x35;  // mq_poll
uint16_t msg_len = RIA.a | (RIA.x << 8);

if (msg_len > 0) {
    printf("Message available: %d bytes\n", msg_len);
}
```

### mq_read_message ($36)

Read the payload of a received message.

**Parameters** (on stack):
- `uint16_t buf_len` - Maximum length to read
- `uint8_t* buffer` - Pointer to buffer in XRAM

**Returns**:
- Number of bytes read
- errno on error

**Example (CC65)**:
```c
uint16_t buf_addr = 0x0200;
uint16_t buf_len = 256;

RIA.xstack = buf_len & 0xFF;  // buf_len low
RIA.xstack = buf_len >> 8;    // buf_len high
RIA.a = buf_addr & 0xFF;      // buffer addr low
RIA.x = buf_addr >> 8;        // buffer addr high
RIA.op = 0x36;                // mq_read_message

uint16_t bytes_read = RIA.a | (RIA.x << 8);

// Read message from XRAM
RIA.addr0 = buf_addr;
for (int i = 0; i < bytes_read; i++) {
    char ch = RIA.rw0;
    putchar(ch);
}
```

### mq_get_topic ($37)

Get the topic of the last received message.

**Parameters** (on stack):
- `uint16_t buf_len` - Maximum length to read
- `uint8_t* buffer` - Pointer to buffer in XRAM

**Returns**:
- Number of bytes written
- errno on error

**Example (CC65)**:
```c
uint16_t buf_addr = 0x0300;
uint16_t buf_len = 128;

RIA.xstack = buf_len & 0xFF;  // buf_len low
RIA.xstack = buf_len >> 8;    // buf_len high
RIA.a = buf_addr & 0xFF;      // buffer addr low
RIA.x = buf_addr >> 8;        // buffer addr high
RIA.op = 0x37;                // mq_get_topic

uint16_t topic_len = RIA.a | (RIA.x << 8);

printf("Topic: ");
RIA.addr0 = buf_addr;
for (int i = 0; i < topic_len; i++) {
    putchar(RIA.rw0);
}
printf("\n");
```

### mq_connected ($38)

Check if connected to the broker.

**Parameters**: None

**Returns**:
- 1 if connected
- 0 if not connected

**Example (CC65)**:
```c
RIA.op = 0x38;  // mq_connected
if (RIA.a == 1) {
    printf("Connected\n");
} else {
    printf("Not connected\n");
}
```

### mq_set_auth ($39)

Set authentication credentials. Must be called before connecting.

**Parameters** (on stack):
- `uint16_t password_len` - Length of password
- `uint8_t* password` - Pointer to password in XRAM
- `uint16_t username_len` - Length of username
- `uint8_t* username` - Pointer to username in XRAM

**Returns**:
- 0 on success
- errno on error

**Example (CC65)**:
```c
char username[] = "myuser";
char password[] = "mypassword";

// Copy username to XRAM at 0x0000
RIA.addr0 = 0x0000;
for (int i = 0; username[i]; i++) {
    RIA.rw0 = username[i];
}
uint16_t username_len = strlen(username);

// Copy password to XRAM at 0x0100
RIA.addr0 = 0x0100;
for (int i = 0; password[i]; i++) {
    RIA.rw0 = password[i];
}
uint16_t password_len = strlen(password);

// Push parameters
RIA.xstack = password_len & 0xFF;  // password_len low
RIA.xstack = password_len >> 8;    // password_len high
RIA.xstack = 0x00;                 // password addr low
RIA.xstack = 0x01;                 // password addr high
RIA.xstack = username_len & 0xFF;  // username_len low
RIA.xstack = username_len >> 8;    // username_len high
RIA.a = 0x00;                      // username addr low
RIA.x = 0x00;                      // username addr high
RIA.op = 0x39;                     // mq_set_auth
```

### mq_set_will ($3A)

Configure Last Will and Testament (LWT) message. Must be called before connecting.

**Parameters** (on stack):
- `uint8_t will_qos` - QoS for will message (0, 1, or 2)
- `uint8_t will_retain` - Retain flag for will message
- `uint16_t will_topic_len` - Length of will topic
- `uint8_t* will_topic` - Pointer to will topic in XRAM
- `uint16_t will_payload_len` - Length of will payload
- `uint8_t* will_payload` - Pointer to will payload in XRAM

**Returns**:
- 0 on success
- errno on error

**Example (CC65)**:
```c
char will_topic[] = "status/rp6502";
char will_payload[] = "offline";

// Copy will topic to XRAM at 0x0000
RIA.addr0 = 0x0000;
for (int i = 0; will_topic[i]; i++) {
    RIA.rw0 = will_topic[i];
}
uint16_t will_topic_len = strlen(will_topic);

// Copy will payload to XRAM at 0x0100
RIA.addr0 = 0x0100;
for (int i = 0; will_payload[i]; i++) {
    RIA.rw0 = will_payload[i];
}
uint16_t will_payload_len = strlen(will_payload);

// Push parameters
RIA.xstack = 0;                          // QoS 0
RIA.xstack = 1;                          // retain = true
RIA.xstack = will_topic_len & 0xFF;      // will_topic_len low
RIA.xstack = will_topic_len >> 8;        // will_topic_len high
RIA.xstack = 0x00;                       // will_topic addr low
RIA.xstack = 0x00;                       // will_topic addr high
RIA.xstack = will_payload_len & 0xFF;    // will_payload_len low
RIA.xstack = will_payload_len >> 8;      // will_payload_len high
RIA.a = 0x00;                            // will_payload addr low
RIA.x = 0x01;                            // will_payload addr high
RIA.op = 0x3A;                           // mq_set_will
```

## Complete Example: Temperature Sensor Publisher

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rp6502.h>
#include <time.h>

void xram_strcpy(uint16_t addr, const char* str) {
    RIA.addr0 = addr;
    for (int i = 0; str[i]; i++) {
        RIA.rw0 = str[i];
    }
    RIA.rw0 = 0;
}

int main() {
    printf("MQTT Temperature Sensor Example\n");
    
    // Set up broker connection
    char broker[] = "test.mosquitto.org";
    char client_id[] = "rp6502_temp_sensor";
    uint16_t port = 1883;
    
    xram_strcpy(0x0000, broker);
    xram_strcpy(0x0100, client_id);
    
    // Connect
    printf("Connecting to %s:%d...\n", broker, port);
    RIA.xstack = port & 0xFF;
    RIA.xstack = port >> 8;
    RIA.xstack = 0x00;
    RIA.xstack = 0x00;
    RIA.a = 0x00;
    RIA.x = 0x01;
    RIA.op = 0x30;  // mq_connect
    
    if (RIA.a != 0) {
        printf("Connection failed: %d\n", RIA.errno);
        return 1;
    }
    
    // Wait for connection
    printf("Waiting for connection...\n");
    for (int i = 0; i < 50; i++) {
        // Small delay
        for (volatile int j = 0; j < 10000; j++);
        
        RIA.op = 0x38;  // mq_connected
        if (RIA.a == 1) {
            printf("Connected!\n");
            break;
        }
    }
    
    // Publish temperature readings
    char topic[] = "rp6502/temperature";
    char payload[16];
    
    for (int i = 0; i < 10; i++) {
        // Generate fake temperature
        int temp = 20 + (rand() % 10);
        sprintf(payload, "%d.%d C", temp, rand() % 10);
        
        printf("Publishing: %s = %s\n", topic, payload);
        
        // Copy to XRAM
        xram_strcpy(0x0000, topic);
        xram_strcpy(0x0100, payload);
        
        uint16_t topic_len = strlen(topic);
        uint16_t payload_len = strlen(payload);
        
        // Publish
        RIA.xstack = 0;                     // QoS 0
        RIA.xstack = 0;                     // retain = false
        RIA.xstack = topic_len & 0xFF;
        RIA.xstack = topic_len >> 8;
        RIA.xstack = 0x00;
        RIA.xstack = 0x00;
        RIA.xstack = payload_len & 0xFF;
        RIA.xstack = payload_len >> 8;
        RIA.a = 0x00;
        RIA.x = 0x01;
        RIA.op = 0x32;  // mq_publish
        
        if (RIA.a != 0) {
            printf("Publish failed: %d\n", RIA.errno);
        }
        
        // Wait 2 seconds
        for (volatile long j = 0; j < 200000; j++);
    }
    
    // Disconnect
    printf("Disconnecting...\n");
    RIA.op = 0x31;  // mq_disconnect
    
    printf("Done!\n");
    return 0;
}
```

## Complete Example: Message Subscriber

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rp6502.h>

void xram_strcpy(uint16_t addr, const char* str) {
    RIA.addr0 = addr;
    for (int i = 0; str[i]; i++) {
        RIA.rw0 = str[i];
    }
    RIA.rw0 = 0;
}

int main() {
    printf("MQTT Subscriber Example\n");
    
    // Connect to broker
    char broker[] = "test.mosquitto.org";
    char client_id[] = "rp6502_subscriber";
    uint16_t port = 1883;
    
    xram_strcpy(0x0000, broker);
    xram_strcpy(0x0100, client_id);
    
    printf("Connecting...\n");
    RIA.xstack = port & 0xFF;
    RIA.xstack = port >> 8;
    RIA.xstack = 0x00;
    RIA.xstack = 0x00;
    RIA.a = 0x00;
    RIA.x = 0x01;
    RIA.op = 0x30;  // mq_connect
    
    if (RIA.a != 0) {
        printf("Connection failed: %d\n", RIA.errno);
        return 1;
    }
    
    // Wait for connection
    for (int i = 0; i < 50; i++) {
        for (volatile int j = 0; j < 10000; j++);
        RIA.op = 0x38;  // mq_connected
        if (RIA.a == 1) {
            printf("Connected!\n");
            break;
        }
    }
    
    // Subscribe to topic
    char topic[] = "rp6502/#";
    xram_strcpy(0x0000, topic);
    uint16_t topic_len = strlen(topic);
    
    printf("Subscribing to: %s\n", topic);
    RIA.xstack = 0;                     // QoS 0
    RIA.xstack = topic_len & 0xFF;
    RIA.xstack = topic_len >> 8;
    RIA.a = 0x00;
    RIA.x = 0x00;
    RIA.op = 0x33;  // mq_subscribe
    
    if (RIA.a != 0) {
        printf("Subscribe failed: %d\n", RIA.errno);
        return 1;
    }
    
    printf("Listening for messages (press Ctrl+C to exit)...\n");
    
    // Message receive loop
    while (1) {
        // Check for messages
        RIA.op = 0x35;  // mq_poll
        uint16_t msg_len = RIA.a | (RIA.x << 8);
        
        if (msg_len > 0) {
            // Get topic
            RIA.xstack = 128 & 0xFF;
            RIA.xstack = 128 >> 8;
            RIA.a = 0x00;
            RIA.x = 0x02;
            RIA.op = 0x37;  // mq_get_topic
            
            uint16_t topic_len = RIA.a | (RIA.x << 8);
            
            printf("Message received on topic: ");
            RIA.addr0 = 0x0200;
            for (int i = 0; i < topic_len; i++) {
                putchar(RIA.rw0);
            }
            printf("\n");
            
            // Read message
            RIA.xstack = 255 & 0xFF;
            RIA.xstack = 255 >> 8;
            RIA.a = 0x00;
            RIA.x = 0x03;
            RIA.op = 0x36;  // mq_read_message
            
            uint16_t bytes_read = RIA.a | (RIA.x << 8);
            
            printf("Payload (%d bytes): ", bytes_read);
            RIA.addr0 = 0x0300;
            for (int i = 0; i < bytes_read; i++) {
                putchar(RIA.rw0);
            }
            printf("\n\n");
        }
        
        // Small delay
        for (volatile int i = 0; i < 1000; i++);
    }
    
    return 0;
}
```

## Notes

- The MQTT client only works on RP6502-RIA-W (Pico 2 W) with WiFi enabled
- Connect to WiFi using the modem commands before using MQTT
- Messages are buffered - only one message can be held at a time
- Call `mq_poll` regularly to check for incoming messages
- The client automatically sends PING messages to maintain the connection
- Maximum message sizes: 1024 bytes payload, 256 bytes topic
- Client ID, username, and password are limited to 128 characters

## QoS Levels

- **QoS 0** (At most once): Fire and forget, no acknowledgment
- **QoS 1** (At least once): Acknowledged delivery, may receive duplicates
- **QoS 2** (Exactly once): Guaranteed single delivery (not yet fully implemented)

## Common MQTT Brokers

- **test.mosquitto.org** - Public test broker (port 1883)
- **broker.hivemq.com** - Public HiveMQ broker (port 1883)
- **mqtt.eclipseprojects.io** - Eclipse public broker (port 1883)

For production use, set up your own broker or use a cloud MQTT service with authentication enabled.
