import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID
from esphome.components import switch, sensor, binary_sensor, button

DEPENDENCIES = ['switch', 'sensor', 'binary_sensor','button']

CONF_CONTROL_SWITCH = 'control_switch'
CONF_CONTROL_AC = 'control_ac'
CONF_RELAYS = 'relays'
CONF_ANALOG_SENSORS = 'analog_sensors'
CONF_BINARY_SENSORS = 'binary_sensors'
CONF_MODBUS_SENSORS = 'modbus_sensors'
CONF_BUTTONS = 'buttons'
CONF_OUTPUT_SENSORS = 'output_sensors'  # датчики для вывода значений

generator_control_ns = cg.esphome_ns.namespace('generator_control')
GeneratorControl = generator_control_ns.class_('GeneratorControl', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(GeneratorControl),
    cv.Required(CONF_CONTROL_SWITCH): cv.use_id(switch.Switch),
    cv.Required(CONF_CONTROL_AC): cv.use_id(switch.Switch),
    cv.Required(CONF_RELAYS): cv.ensure_list(cv.use_id(switch.Switch)),
    cv.Optional(CONF_ANALOG_SENSORS): cv.ensure_list(cv.use_id(sensor.Sensor)),
    cv.Optional(CONF_BINARY_SENSORS): cv.ensure_list(cv.use_id(binary_sensor.BinarySensor)),
    cv.Optional(CONF_MODBUS_SENSORS): cv.ensure_list(cv.use_id(sensor.Sensor)),
    cv.Optional(CONF_BUTTONS): cv.ensure_list(cv.use_id(button.Button)),
    cv.Optional(CONF_OUTPUT_SENSORS): cv.ensure_list(cv.use_id(sensor.Sensor)),  
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    control_switch = await cg.get_variable(config[CONF_CONTROL_SWITCH])
    cg.add(var.set_control_switch(control_switch))
    
    control_ac = await cg.get_variable(config[CONF_CONTROL_AC])
    cg.add(var.set_control_ac(control_ac))
    
    for relay_id in config[CONF_RELAYS]:
        relay = await cg.get_variable(relay_id)
        cg.add(var.add_relay(relay))
        
    if CONF_ANALOG_SENSORS in config:
        for sensor_id in config[CONF_ANALOG_SENSORS]:
            analog_sensor = await cg.get_variable(sensor_id)
            cg.add(var.add_analog_sensor(analog_sensor))
            
    if CONF_BINARY_SENSORS in config:
        for sensor_id in config[CONF_BINARY_SENSORS]:
            binary_sensor = await cg.get_variable(sensor_id)
            cg.add(var.add_binary_sensor(binary_sensor))
            
    if CONF_MODBUS_SENSORS in config:
        for sensor_id in config[CONF_MODBUS_SENSORS]:
            modbus_sensor = await cg.get_variable(sensor_id)
            cg.add(var.add_modbus_sensor(modbus_sensor))
            
    if CONF_BUTTONS in config:
        for button_id in config[CONF_BUTTONS]:
            btn = await cg.get_variable(button_id)
            cg.add(var.add_button(btn))
            
    if CONF_OUTPUT_SENSORS in config:
        for sensor_id in config[CONF_OUTPUT_SENSORS]:
            output_sensor = await cg.get_variable(sensor_id)
            cg.add(var.add_output_sensor(output_sensor))        
            