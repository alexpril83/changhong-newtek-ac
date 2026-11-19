import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, sensor, switch, uart
from esphome.const import CONF_ID, ENTITY_CATEGORY_DIAGNOSTIC

DEPENDENCIES = ["climate", "uart"]
AUTO_LOAD = ["switch", "sensor"]

CONF_DISPLAY_SWITCH = "display_switch"
CONF_ERROR_CODE = "error_code"

changhong_ac_ns = cg.esphome_ns.namespace("changhong_ac")
ChanghongAC = changhong_ac_ns.class_(
    "ChanghongAC", climate.Climate, uart.UARTDevice, cg.PollingComponent
)
ChanghongDisplaySwitch = changhong_ac_ns.class_(
    "ChanghongDisplaySwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = (
    climate.climate_schema(ChanghongAC)
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.polling_component_schema("30s"))
    .extend(
        {
            cv.Optional(CONF_DISPLAY_SWITCH): switch.switch_schema(
                ChanghongDisplaySwitch,
                icon="mdi:led-on",
            ),
            cv.Optional(CONF_ERROR_CODE): sensor.sensor_schema(
                accuracy_decimals=0,
                icon="mdi:alert-circle",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    )
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    await uart.register_uart_device(var, config)

    if CONF_DISPLAY_SWITCH in config:
        sw = await switch.new_switch(config[CONF_DISPLAY_SWITCH])
        await cg.register_component(sw, config[CONF_DISPLAY_SWITCH])
        cg.add(var.set_display_switch(sw))

    if CONF_ERROR_CODE in config:
        sens = await sensor.new_sensor(config[CONF_ERROR_CODE])
        cg.add(var.set_error_code_sensor(sens))
