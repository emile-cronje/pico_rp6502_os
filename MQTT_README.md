# MQTT Client Implementation for RP6502

This implementation adds full MQTT 3.1.1 client support to the RP6502 RIA firmware, allowing 6502 applications to communicate with MQTT brokers over WiFi.

## Files Added/Modified

### New Files Created

1. **src/ria/net/mq.h** - MQTT client header file
   - Defines all public API functions
   - Declares main event handlers (init, task, stop)
   - API operation prototypes for 6502 interface

2. **src/ria/net/mq.c** - MQTT client implementation
   - Full MQTT 3.1.1 protocol implementation
   - lwIP TCP/IP integration
   - DNS resolution for broker hostname
   - Connection management with keepalive
   - Publish/subscribe functionality
   - Message buffering and parsing
   - Authentication and Last Will support

3. **MQTT_API.md** - Complete API documentation
   - Detailed function descriptions
   - CC65 code examples
   - Two complete working examples (publisher and subscriber)
   - Usage notes and common brokers list

### Modified Files

1. **src/ria/main.c**
   - Added `#include "net/mq.h"`
   - Added `mq_init()` to initialization sequence
   - Added `mq_task()` to main task loop
   - Added `mq_stop()` to stop sequence
   - Added 11 new API operations (0x30-0x3A) for MQTT functions

2. **src/CMakeLists.txt**
   - Added `ria/net/mq.c` to source file list

## Features

### Core MQTT Protocol Support
- ✅ CONNECT with configurable client ID
- ✅ DISCONNECT with proper cleanup
- ✅ PUBLISH with QoS 0, 1, 2 (send side)
- ✅ SUBSCRIBE with topic filters
- ✅ UNSUBSCRIBE
- ✅ PINGREQ/PINGRESP for keepalive
- ✅ Username/password authentication
- ✅ Last Will and Testament (LWT)

### Additional Features
- ✅ DNS hostname resolution
- ✅ Automatic keepalive (60 second interval)
- ✅ Message buffering (1 message at a time)
- ✅ QoS support for both publish and subscribe
- ✅ Retain flag support
- ✅ Clean session
- ✅ Configurable broker and port

### 6502 API Operations

| Operation | Code | Function |
|-----------|------|----------|
| mq_connect | 0x30 | Connect to MQTT broker |
| mq_disconnect | 0x31 | Disconnect from broker |
| mq_publish | 0x32 | Publish a message |
| mq_subscribe | 0x33 | Subscribe to a topic |
| mq_unsubscribe | 0x34 | Unsubscribe from a topic |
| mq_poll | 0x35 | Check for incoming messages |
| mq_read_message | 0x36 | Read message payload |
| mq_get_topic | 0x37 | Get message topic |
| mq_connected | 0x38 | Check connection status |
| mq_set_auth | 0x39 | Set credentials |
| mq_set_will | 0x3A | Configure LWT |

## Building

The MQTT client will be included automatically when building for RP6502-RIA-W (Pico 2 W) boards:

```bash
cd build
cmake -DPICO_BOARD=pico2_w ..
ninja
```

For non-WiFi boards, the MQTT functions compile to empty stubs that return immediately.

## Usage from 6502 Applications

### Prerequisites
1. RP6502-RIA-W hardware (Pico 2 W with WiFi)
2. WiFi connection configured (use modem AT commands)
3. Access to an MQTT broker

### Basic Flow
1. Set authentication (optional): `mq_set_auth` (0x39)
2. Set Last Will (optional): `mq_set_will` (0x3A)
3. Connect to broker: `mq_connect` (0x30)
4. Wait for connection: check with `mq_connected` (0x38)
5. Subscribe/publish: `mq_subscribe` (0x33) / `mq_publish` (0x32)
6. Poll for messages: `mq_poll` (0x35)
7. Read messages: `mq_get_topic` (0x37) + `mq_read_message` (0x36)
8. Disconnect: `mq_disconnect` (0x31)

See [MQTT_API.md](MQTT_API.md) for complete examples.

## Architecture

### Integration with RIA System
- **Initialization**: `mq_init()` called during system startup
- **Task Loop**: `mq_task()` called in main event loop for keepalive
- **Stop Handler**: `mq_stop()` called when 6502 stops
- **API Dispatcher**: Operations routed through `main_api()` in main.c

### Network Stack
- Uses lwIP TCP/IP stack (already present in RIA-W builds)
- TCP connection for MQTT protocol
- DNS resolution for broker hostnames
- Automatic reconnection not implemented (application responsibility)

### Memory Layout
- TX buffer: 1024 bytes
- RX buffer: 2048 bytes  
- Topic buffer: 256 bytes
- Payload buffer: 1024 bytes
- Credentials: 128 bytes each (username, password, client_id)

### Protocol Implementation
- MQTT 3.1.1 compliant
- Variable header encoding/decoding
- Remaining length encoding (up to 268,435,455 bytes)
- Packet ID management for QoS > 0
- Message parsing with proper state machine

## Limitations

1. **Single Message Buffer**: Only one received message can be held at a time. Poll and read frequently.

2. **QoS 2 Receive**: QoS 2 message reception not fully implemented (packets acknowledged but not deduplicated).

3. **No Reconnection**: Automatic reconnection not implemented. Application must detect disconnection and reconnect.

4. **No TLS**: Plain TCP only, no SSL/TLS support (lwIP limitation without mbedTLS).

5. **WiFi Only**: Requires RP6502-RIA-W hardware with WiFi enabled.

6. **Message Size**: Maximum 1KB payload, 256 byte topic.

## Testing

### Test with Mosquitto Clients

Publisher:
```bash
mosquitto_pub -h test.mosquitto.org -t "rp6502/test" -m "Hello from Linux"
```

Subscriber:
```bash
mosquitto_sub -h test.mosquitto.org -t "rp6502/#" -v
```

### Public Test Brokers
- test.mosquitto.org:1883
- broker.hivemq.com:1883
- mqtt.eclipseprojects.io:1883

## Future Enhancements

Possible improvements for future versions:

- [ ] TLS/SSL support (requires mbedTLS integration)
- [ ] Automatic reconnection with exponential backoff
- [ ] Multiple message queue
- [ ] QoS 2 full implementation
- [ ] Persistent sessions
- [ ] Message statistics/diagnostics
- [ ] MQTT 5.0 support

## License

BSD-3-Clause (same as RP6502 project)

## Author

Implementation created for the RP6502 project.
GitHub: https://github.com/picocomputer/rp6502
