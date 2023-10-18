import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, esp32_ble_tracker
from esphome.const import (
    CONF_MAC_ADDRESS,
    CONF_ID,
    CONF_BINDKEY,
)

CODEOWNERS = ["@chbgdn"]

DEPENDENCIES = ["esp32_ble_tracker"]

MULTI_CONF = True

bthome_mithermometer_ns = cg.esphome_ns.namespace("bthome_mithermometer")
BTHomeMiThermometer = bthome_mithermometer_ns.class_(
    "BTHomeMiThermometer", esp32_ble_tracker.ESPBTDeviceListener, cg.Component
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BTHomeMiThermometer),
            cv.Required(CONF_MAC_ADDRESS): cv.mac_address,
            cv.Optional(CONF_BINDKEY): cv.bind_key,
        }
    )
    .extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await esp32_ble_tracker.register_ble_device(var, config)

    cg.add(var.set_address(config[CONF_MAC_ADDRESS].as_hex))
    if CONF_BINDKEY in config:
        cg.add(var.set_bindkey(config[CONF_BINDKEY]))
