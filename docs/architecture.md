# TMBox local-first architecture (v1)

## Product decisions

- A logical TMBox always exposes A, B, C and D. Each position is mapped to a
  configured station connection or left unused.
- Raspberry Pi is the only operational authority during a running session.
- Swift, the local web client and ESP32 boxes are panel clients. They
  send key presses and render authoritative snapshots; they do not decide
  whether a movement is safe.
- MQTT is client/server transport through the Pi's local broker. Boxes never
  trust or command each other directly.
- The active run continues over the local network when internet or TrainMeet
  cloud access is unavailable.
- The Pi can own a complete local configuration. TrainMeet centrally can also
  build and publish a validated release that the Pi installs for the active run.
- The Pi also serves a local web TMBox and the simple device-assignment UI.
- Local clients do not use MQTT passwords. A permanent device id and Pi-owned
  panel mapping are the usability-focused boundary on the isolated meeting
  network.

## Operation modes

- `clearance`: a single-track request must be accepted by the receiving
  station before departure can be reserved.
- `direct`: the first station reserves a free line immediately. The other
  direction is blocked by the Pi.
- Double track is modeled as independent directed channels so incoming and
  outgoing traffic can coexist. This is the next expansion after the current
  single-track vertical slice.

## TMBox compatibility rule

The native and web TMBox surfaces retain the palette, case, 16×2 LCD,
character cells, keypad dimensions and key order established by the original
TrainMeet simulator and the physical box.

The Pi owns the common display renderer. Golden-master tests currently lock:

1. idle panel
2. destination and train-number entry
3. request waiting for a response
4. ready for departure
5. final departure confirmation

The implementation follows the intended Lovable workflow but keeps safety
improvements that belong on the Pi: atomic state transitions, command expiry,
revision checks, command-id idempotency and one input owner at a time.

## MQTT v1

```text
client -> tambox/v1/client/{client_id}/command
Pi     -> tambox/v1/client/{client_id}/ack
Pi     -> tambox/v1/client/{client_id}/snapshot/{panel_id}
client -> tambox/v1/client/{client_id}/presence
box    -> tambox/v1/device/{device_id}/hello
Pi     -> tambox/v1/device/{device_id}/assignment
Pi     -> tambox/v1/gateway/{gateway_id}/status
```

Commands, acknowledgements and snapshots use QoS 1. Only snapshots and status
are retained. Every mutating command contains a unique id, expected server
revision and a short expiry time.

## Implemented vertical slice

- two stations and one single-track connection
- A on both logical panels
- clearance and direct modes
- request, accept, reject, cancel, departure, arrival and release
- full panel snapshots with exact 16-character rows
- stale, expired and duplicate command handling
- multiple clients viewing one panel with input ownership
- separate CocoaMQTT Swift client and Eclipse Paho Pi adapter
- real Mosquitto loopback integration test, including reconnect
- transactional SQLite state, command cache and journal restoration after a
  Raspberry Pi process restart
- fail-closed configuration fingerprint validation and memory rollback if a
  durable write fails
- durable identity registry, six-digit Swift/web pairing and panel assignments
- passwordless physical-box discovery by printed code
- Pi-hosted responsive web TMBox and physical-device mapping
- one-command Mac runner and Raspberry Pi system service installer
- ESP32 firmware with permanent device identity, captive Wi-Fi setup, mDNS
  server discovery, QoS 1 commands and automatic reconnect

## Next slices

1. double-track directed channels and 1.5-second alternating classic display
2. local/TrainMeet clock adapters and stopped-clock messages
3. hardware validation against Bennys original ESP32 box and final pin profile
