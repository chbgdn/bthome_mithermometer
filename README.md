```
external_components:
  - source: github://chbgdn/bthome_mithermometer@main
    refresh: 0s

sensor:
  - platform: bthome_mithermometer
    mac_address: "00:11:22:33:44:55"
    bindkey: "12345678901234567890123456789012" # optional
    temperature:
      name: "Temperature"
    humidity:
      name: "Humidity"
    battery_level:
      name: "Battery-Level"
    battery_voltage:
      name: "Battery-Voltage"
    signal_strength:
      name: "Signal"
```
