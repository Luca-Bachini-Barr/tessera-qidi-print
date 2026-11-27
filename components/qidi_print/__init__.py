"""QIDI print: the ESPHome side. register_plugin() reads the id, version, tile memory and the screen's texts from the
manifest and translations/ beside this folder (docs/FIRMWARE_API.md of the Tessera plugins repository)."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import smart_display
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display"]

qidi_print_ns = cg.esphome_ns.namespace("qidi_print")
QidiPrint = qidi_print_ns.class_("QidiPrint", cg.Component)

CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(QidiPrint)}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
