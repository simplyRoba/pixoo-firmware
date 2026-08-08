import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_CONDITION,
    CONF_ID,
    CONF_LATITUDE,
    CONF_LONGITUDE,
)

CONF_TEMPERATURE = "temperature"
CONF_APPARENT_TEMPERATURE = "apparent_temperature"
CONF_HUMIDITY = "humidity"
CONF_HIGH = "high"
CONF_LOW = "low"
CONF_START_HOUR = "start_hour"
CONF_FORECAST_TEMPERATURES = "forecast_temperatures"
CONF_NIGHT = "night"
CONF_LOCATION = "location"
CONF_AVAILABLE_AT_MS = "available_at_ms"
CONF_CONDITION_AFTER = "condition_after"
CONF_CONDITION_AFTER_MS = "condition_after_ms"

pixoo_ns = cg.global_ns.namespace("pixoo")
WeatherCondition = pixoo_ns.enum("WeatherCondition", is_class=True)
WeatherSource = pixoo_ns.class_("WeatherSource")
render_test_ns = cg.esphome_ns.namespace("pixoo64_render_test")
StaticWeatherSource = render_test_ns.class_("StaticWeatherSource", WeatherSource)

# The renderer's condition vocabulary, named so the test config stays inside the
# closed set the domain enum defines.
CONDITIONS = {
    "sunny": WeatherCondition.SUNNY,
    "partlycloudy": WeatherCondition.PARTLYCLOUDY,
    "cloudy": WeatherCondition.CLOUDY,
    "fog": WeatherCondition.FOG,
    "drizzle": WeatherCondition.DRIZZLE,
    "freezing-drizzle": WeatherCondition.FREEZING_DRIZZLE,
    "rainy": WeatherCondition.RAINY,
    "pouring": WeatherCondition.POURING,
    "freezing-rain": WeatherCondition.FREEZING_RAIN,
    "snowy": WeatherCondition.SNOWY,
    "snow-grains": WeatherCondition.SNOW_GRAINS,
    "thunderstorm": WeatherCondition.THUNDERSTORM,
    "hail-thunderstorm": WeatherCondition.HAIL_THUNDERSTORM,
    "unknown": WeatherCondition.UNKNOWN,
}

MULTI_CONF = True


def validate_condition_after(config):
    has_condition = CONF_CONDITION_AFTER in config
    has_time = CONF_CONDITION_AFTER_MS in config
    if has_condition != has_time:
        raise cv.Invalid(
            "condition_after and condition_after_ms must be configured together"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.declare_id(StaticWeatherSource),
            cv.Required(CONF_CONDITION): cv.enum(CONDITIONS, lower=True),
            cv.Optional(CONF_AVAILABLE_AT_MS): cv.int_range(
                min=0, max=0xFFFFFFFF
            ),
            cv.Optional(CONF_CONDITION_AFTER): cv.enum(CONDITIONS, lower=True),
            cv.Optional(CONF_CONDITION_AFTER_MS): cv.int_range(
                min=0, max=0xFFFFFFFF
            ),
            cv.Optional(CONF_NIGHT, default=False): cv.boolean,
            cv.Required(CONF_TEMPERATURE): cv.float_,
            cv.Required(CONF_APPARENT_TEMPERATURE): cv.float_,
            cv.Required(CONF_HUMIDITY): cv.float_,
            cv.Required(CONF_HIGH): cv.float_,
            cv.Required(CONF_LOW): cv.float_,
            cv.Required(CONF_START_HOUR): cv.int_range(min=0, max=23),
            cv.Required(CONF_FORECAST_TEMPERATURES): cv.ensure_list(cv.float_),
            cv.Optional(CONF_LOCATION): cv.Schema(
                {
                    cv.Required(CONF_LATITUDE): cv.float_range(min=-90, max=90),
                    cv.Required(CONF_LONGITUDE): cv.float_range(min=-180, max=180),
                }
            ),
        }
    ),
    validate_condition_after,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_condition(config[CONF_CONDITION]))
    if CONF_AVAILABLE_AT_MS in config:
        cg.add(var.set_available_at(config[CONF_AVAILABLE_AT_MS]))
    if CONF_CONDITION_AFTER in config:
        cg.add(
            var.set_condition_after(
                config[CONF_CONDITION_AFTER], config[CONF_CONDITION_AFTER_MS]
            )
        )
    cg.add(var.set_night(config[CONF_NIGHT]))
    cg.add(
        var.set_current(
            config[CONF_TEMPERATURE],
            config[CONF_APPARENT_TEMPERATURE],
            config[CONF_HUMIDITY],
        )
    )
    cg.add(var.set_daily(config[CONF_HIGH], config[CONF_LOW]))
    cg.add(var.set_start_hour(config[CONF_START_HOUR]))
    for t in config[CONF_FORECAST_TEMPERATURES]:
        cg.add(var.add_forecast_temperature(t))
    if CONF_LOCATION in config:
        loc = config[CONF_LOCATION]
        cg.add(var.set_location(loc[CONF_LATITUDE], loc[CONF_LONGITUDE]))
    return var
