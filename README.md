# ESPHome Midea X3

External ESPHome component fork for Midea air conditioners.

This repository starts as a narrow copy of the upstream ESPHome `midea`
component, so it can be used through `external_components` without forking
all of ESPHome.

## Origin

- Upstream: https://github.com/esphome/esphome
- Upstream component path: `esphome/components/midea`
- Initial upstream ref: `792dfbcbbf116002468ed3692596fa5aa88b9448`

The first import intentionally keeps the upstream component code unchanged.
Project-specific additions should be small, reviewable commits on top.

## License

This repository keeps the ESPHome license terms.

ESPHome uses a mixed license model:

- C++ runtime files (`.c`, `.cpp`, `.h`, `.hpp`, `.tcc`, `.ino`) are GPLv3.
- Python and other files are MIT.

See `LICENSE` for the complete upstream license text.

## ESPHome Usage

Complete test configuration:

- [`examples/midea-uart-ir.yaml`](examples/midea-uart-ir.yaml)

```yaml
external_components:
  - source: github://x3muha/esphome-midea-x3@main
    components: [midea]
```

Then use the normal ESPHome Midea configuration:

```yaml
climate:
  - platform: midea
    id: midea_ac
    name: "Klimaanlage"
    uart_id: ac_uart
    autoconf: true
```

## Local Goals

Planned extensions:

- expose communication health and last error as ESPHome entities
- expose last control source, including IR input detection
- expose raw status/debug data for Midea UART frames
- model Clean as an estimated ESP-side state when no reliable device feedback exists
- add Display on/off state handling where possible
- provide API-friendly entities for EDOMI integration

## X3 Fresh/Clean Options

This fork adds demodulated TSOP-line Midea actions for Fresh and Clean. These
actions are meant for hardware where the ESP output is connected to the indoor
unit TSOP output line, not to an IR LED.

```yaml
climate:
  - platform: midea
    id: midea_ac
    name: "Klimaanlage"
    uart_id: ac_uart
    transmitter_id: ir_tx
    autoconf: true
    clean_restore: true
    clean_duration: 120min
    fresh_state:
      id: midea_fresh_state
      name: "Midea Fresh Status"
      internal: true
    clean_running:
      id: midea_clean_running
      name: "Midea Clean Running"
      internal: true
    clean_state:
      name: "Midea Clean State"
    clean_remaining:
      name: "Midea Clean Remaining"
    last_control_source:
      name: "Midea Last Control Source"
```

Available actions:

```yaml
midea_ac.fresh_on:
  id: midea_ac
midea_ac.fresh_off:
  id: midea_ac
midea_ac.clean_on:
  id: midea_ac
midea_ac.clean_off:
  id: midea_ac
midea_ac.clean_reset:
  id: midea_ac
```

`clean_restore: true` stores the current climate mode, target temperature, fan
mode, and Fresh state before starting Clean. After `clean_duration` expires, the
component restores that climate state and re-enables Fresh if it was previously
on.

Fresh state is synchronized from UART `C0` status frames. On the tested X3 unit,
the Fresh bit is `frame[19] & 0x20`, corresponding to status payload byte 9 when
counting from `C0`. In captured frames this appears as `... 20 20 00 ...` for
Fresh on and `... 20 00 00 ...` for Fresh off. The component does not publish
Fresh state optimistically after API/IR commands; it waits for the next UART
status frame. In YAML, publish the visible `Midea Fresh` template switch from the
internal `fresh_state.on_state` trigger so the switch has no guessed boot state.

For normal operation keep `remote_receiver.dump` disabled. With `dump: raw`, the
receiver logs the injected Fresh/Clean timings too, which can produce long-loop
warnings such as `remote_receiver took a long time`.

When the device is controlled through `web_server` REST endpoints only, set
`api.reboot_timeout: 0s`. Otherwise ESPHome's native API can reboot the ESP with
`No client connected to API. Rebooting...` if no Home Assistant, dashboard, or
`esphome logs` client is connected. EDOMI Web API polling does not count as a
native API client. The examples keep the native API encrypted, protect OTA, and
protect the Web API through secrets:

```yaml
api:
  encryption:
    key: !secret api_encryption_key
  reboot_timeout: 0s

ota:
  - platform: esphome
    password: !secret ota_password

web_server:
  port: 80
  auth:
    username: !secret web_user
    password: !secret web_password
  ota: false
```

For Web UI, ESPHome API, KNX, or EDOMI control, keep Fresh and Clean as template
switches in YAML and optionally expose runtime controls for the estimated Clean
logic:

```yaml
number:
  - platform: template
    name: "Midea Clean Dauer"
    id: midea_clean_duration_minutes
    unit_of_measurement: min
    min_value: 15
    max_value: 240
    step: 5
    optimistic: true
    restore_value: true
    initial_value: 120
    set_action:
      - lambda: |-
          id(midea_ac).set_clean_duration(static_cast<uint32_t>(x * 60000.0f));

switch:
  - platform: template
    name: "Midea Clean Restore"
    id: midea_clean_restore_switch
    optimistic: true
    restore_mode: RESTORE_DEFAULT_ON
    turn_on_action:
      - lambda: id(midea_ac).set_clean_restore(true);
    turn_off_action:
      - lambda: id(midea_ac).set_clean_restore(false);
```
