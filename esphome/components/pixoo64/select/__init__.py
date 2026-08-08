import esphome.codegen as cg
from esphome.components import select
from esphome.components import time as time_
import esphome.config_validation as cv
from esphome.const import CONF_RESTORE_VALUE, CONF_TIME_ID, ENTITY_CATEGORY_CONFIG

from .. import pixoo64_ns

TimezoneSelect = pixoo64_ns.class_("TimezoneSelect", select.Select, cg.Component)

# The option labels, their POSIX mapping, and the default live in the C++
# pixoo::TimezoneCatalog (single owner). This platform exposes that catalog as a
# select entity and applies the chosen POSIX string to the time component, so no
# option list is repeated here.
CONFIG_SCHEMA = (
    select.select_schema(TimezoneSelect, entity_category=ENTITY_CATEGORY_CONFIG)
    .extend(
        {
            cv.Required(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
            cv.Optional(CONF_RESTORE_VALUE, default=True): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    # Options are populated at runtime from the catalog in the component's
    # setup(); codegen only needs the entity registered with an empty list.
    var = await select.new_select(config, options=[])
    await cg.register_component(var, config)
    cg.add(var.set_time(await cg.get_variable(config[CONF_TIME_ID])))
    cg.add(var.set_restore_value(config[CONF_RESTORE_VALUE]))
