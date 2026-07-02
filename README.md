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
- handle Fresh state when available from the status frame
- model Clean as an estimated ESP-side state when no reliable device feedback exists
- add Display on/off state handling where possible
- provide API-friendly entities for EDOMI integration
