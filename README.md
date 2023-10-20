```
external_components:
  - source: github://chbgdn/bthome_mithermometer@main
    refresh: 0s

esp32_ble_tracker:
  scan_parameters:
    continuous: true
    active: false

bthome_mithermometer:
  - id: some_unique_id
    mac_address: "00:11:22:AA:BB:CC" # required
    bindkey: "0123456789abcdef0123456789abcdef" # optional

binary_sensor:
  - platform: bthome_mithermometer
    id: some_unique_id
    power:
      name: "Power"

sensor:
  - platform: bthome_mithermometer
    id: some_unique_id
    temperature:
      name: "Temperature"
    humidity:
      name: "Humidity"
    battery_level:
      name: "Battery Level"
    battery_voltage:
      name: "Battery Voltage"
    signal_strength:
      name: "Signal"
```
