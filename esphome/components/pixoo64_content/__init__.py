import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, font, sensor, text, time
from esphome.const import (
    CONF_ID,
    CONF_PLATFORM,
    CONF_TIME_ID,
    CONF_UPDATE_INTERVAL,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_NONE,
    SCHEDULER_DONT_RUN,
    UNIT_MILLISECOND,
)

CONF_DASHBOARD_ID = "dashboard_id"
CONF_FRAME_INTERVAL = "frame_interval"
CONF_DASHBOARDS = "dashboards"
CONF_DEFAULT_DASHBOARD = "default_dashboard"
CONF_PANEL_FONT = "panel_font"
CONF_PANEL_TEXT = "panel_text"
CONF_FONT_SMALL = "font_small"
CONF_FONT_BIG = "font_big"
CONF_FIXED_TIME = "fixed_time"
CONF_SOURCE = "source"
CONF_NOTIFICATION_FONT = "notification_font"
CONF_FIRMWARE_UPDATE_TITLE_FONT = "firmware_update_title_font"
CONF_FIRMWARE_UPDATE_DETAIL_FONT = "firmware_update_detail_font"
CONF_FACE = "face"
CONF_SEED = "seed"
CONF_RENDER_METRICS = "render_metrics"
CONF_RENDER_BUDGET = "budget"
CONF_RENDER_AVERAGE = "average"
CONF_RENDER_MAX = "maximum"
CONF_RENDER_OVER_BUDGET = "over_budget"

AUTO_LOAD = ["time", "font", "sensor"]

pixoo_ns = cg.global_ns.namespace("pixoo")
pixoo64_ns = cg.esphome_ns.namespace("pixoo64")
content_ns = pixoo64_ns.namespace("content")
dashboard_ns = pixoo64_ns.namespace("dashboard")
RenderPort = pixoo_ns.class_("RenderPort")
WeatherSource = pixoo_ns.class_("WeatherSource")
EqualizerLevelsSink = pixoo_ns.class_("EqualizerLevelsSink")
Dashboard = dashboard_ns.class_("Dashboard")
ContentController = content_ns.class_(
    "ContentController", display.Display, RenderPort, EqualizerLevelsSink
)
TextDashboard = dashboard_ns.class_("TextDashboard", Dashboard)
WeatherDashboard = dashboard_ns.class_("WeatherDashboard", Dashboard)
# Closed weather-face vocabulary; each face is a WeatherDashboard subclass bound
# to that face.
WEATHER_FACES = {
    "forecast": dashboard_ns.class_(
        "ForecastWeatherDashboard", WeatherDashboard
    ),
    "landscape": dashboard_ns.class_(
        "LandscapeWeatherDashboard", WeatherDashboard
    ),
}
EqualizerDashboard = dashboard_ns.class_(
    "EqualizerDashboard", Dashboard, EqualizerLevelsSink
)
# Closed equalizer-face vocabulary; each face is an EqualizerDashboard subclass
# bound to that face.
EQUALIZER_FACES = {
    "bars": dashboard_ns.class_("BarsEqualizerDashboard", EqualizerDashboard),
    "waveform": dashboard_ns.class_(
        "WaveformEqualizerDashboard", EqualizerDashboard
    ),
}
ClockDashboard = dashboard_ns.class_("ClockDashboard", Dashboard)
GameOfLifeDashboard = dashboard_ns.class_("GameOfLifeDashboard", Dashboard)
StopwatchDashboard = dashboard_ns.class_("StopwatchDashboard", Dashboard)
# Closed watch-face vocabulary; each face is a ClockDashboard subclass bound to
# that face.
CLOCK_FACES = {
    "split_flap": dashboard_ns.class_("SplitFlapClockDashboard", ClockDashboard),
    "analog": dashboard_ns.class_("AnalogClockDashboard", ClockDashboard),
    "binary": dashboard_ns.class_("BinaryClockDashboard", ClockDashboard),
}

TEXT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TextDashboard),
        cv.Required(CONF_DASHBOARD_ID): cv.string_strict,
        cv.Optional(CONF_FRAME_INTERVAL, default="33ms"):
            cv.positive_time_period_milliseconds,
        cv.Required(CONF_PANEL_FONT): cv.use_id(font.Font),
        cv.Required(CONF_PANEL_TEXT): cv.use_id(text.Text),
    }
)
EQUALIZER_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EqualizerDashboard),
        cv.Required(CONF_DASHBOARD_ID): cv.string_strict,
        cv.Optional(CONF_FRAME_INTERVAL, default="33ms"):
            cv.positive_time_period_milliseconds,
        cv.Required(CONF_FACE): cv.one_of(*EQUALIZER_FACES, lower=True),
    }
)
WEATHER_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WeatherDashboard),
        cv.Required(CONF_DASHBOARD_ID): cv.string_strict,
        cv.Optional(CONF_FRAME_INTERVAL, default="33ms"):
            cv.positive_time_period_milliseconds,
        cv.Required(CONF_FACE): cv.one_of(*WEATHER_FACES, lower=True),
        cv.Required(CONF_FONT_SMALL): cv.use_id(font.Font),
        cv.Required(CONF_FONT_BIG): cv.use_id(font.Font),
        cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        cv.Optional(CONF_FIXED_TIME): cv.positive_int,
        cv.Required(CONF_SOURCE): cv.use_id(WeatherSource),
    }
)
CLOCK_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ClockDashboard),
        cv.Required(CONF_DASHBOARD_ID): cv.string_strict,
        cv.Optional(CONF_FRAME_INTERVAL, default="33ms"):
            cv.positive_time_period_milliseconds,
        cv.Required(CONF_FACE): cv.one_of(*CLOCK_FACES, lower=True),
        cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        cv.Optional(CONF_FIXED_TIME): cv.positive_int,
    }
)
STOPWATCH_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(StopwatchDashboard),
        cv.Required(CONF_DASHBOARD_ID): cv.string_strict,
        cv.Optional(CONF_FRAME_INTERVAL, default="33ms"):
            cv.positive_time_period_milliseconds,
    }
)
GAME_OF_LIFE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GameOfLifeDashboard),
        cv.Required(CONF_DASHBOARD_ID): cv.string_strict,
        cv.Optional(CONF_FRAME_INTERVAL, default="33ms"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SEED): cv.uint32_t,
    }
)
ENTRY_SCHEMA = cv.typed_schema(
    {
        "text": TEXT_SCHEMA,
        "weather": WEATHER_SCHEMA,
        "equalizer": EQUALIZER_SCHEMA,
        "clock": CLOCK_SCHEMA,
        "game_of_life": GAME_OF_LIFE_SCHEMA,
        "stopwatch": STOPWATCH_SCHEMA,
    },
    key=CONF_PLATFORM,
)


def validate_dashboard_config(config):
    ids = set()
    for entry in config[CONF_DASHBOARDS]:
        dashboard_id = entry[CONF_DASHBOARD_ID]
        if not dashboard_id:
            raise cv.Invalid("dashboard_id must not be empty")
        if dashboard_id in ids:
            raise cv.Invalid(f"duplicate dashboard_id: {dashboard_id}")
        ids.add(dashboard_id)
        interval_ms = entry[CONF_FRAME_INTERVAL].total_milliseconds
        if interval_ms < 1:
            raise cv.Invalid("frame_interval must be at least 1 ms")
        if interval_ms > 0x7FFFFFFF:
            raise cv.Invalid(
                "frame_interval must fit a positive signed 32-bit millisecond value"
            )
    stopwatch_entries = [
        entry
        for entry in config[CONF_DASHBOARDS]
        if entry[CONF_PLATFORM] == "stopwatch"
    ]
    if len(stopwatch_entries) > 1:
        raise cv.Invalid("at most one stopwatch dashboard may be configured")
    if (
        stopwatch_entries
        and stopwatch_entries[0][CONF_DASHBOARD_ID] != "stopwatch"
    ):
        raise cv.Invalid("the stopwatch dashboard_id must be stopwatch")
    if config[CONF_DEFAULT_DASHBOARD] not in ids:
        raise cv.Invalid("default_dashboard must name a configured dashboard")
    return config


def validate_render_metrics(config):
    budget_us = config[CONF_RENDER_BUDGET].total_microseconds
    if budget_us <= 0 or budget_us > 0xFFFFFFFF:
        raise cv.Invalid("render budget must fit a positive uint32 microsecond value")
    return config


def validate_render_schedule(config):
    if (
        CONF_RENDER_METRICS in config
        and config[CONF_UPDATE_INTERVAL].total_milliseconds == SCHEDULER_DONT_RUN
    ):
        raise cv.Invalid("render metrics require a finite update_interval")
    return config


RENDER_METRICS_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_RENDER_BUDGET): cv.positive_time_period_microseconds,
            cv.Required(CONF_RENDER_AVERAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_MILLISECOND,
                accuracy_decimals=2,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                state_class=STATE_CLASS_NONE,
            ),
            cv.Required(CONF_RENDER_MAX): sensor.sensor_schema(
                unit_of_measurement=UNIT_MILLISECOND,
                accuracy_decimals=2,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                state_class=STATE_CLASS_NONE,
            ),
            cv.Required(CONF_RENDER_OVER_BUDGET): sensor.sensor_schema(
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                state_class=STATE_CLASS_NONE,
            ),
        }
    ),
    validate_render_metrics,
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ContentController),
            cv.Required(CONF_DEFAULT_DASHBOARD): cv.string_strict,
            cv.Required(CONF_DASHBOARDS): cv.ensure_list(ENTRY_SCHEMA),
            cv.Optional(CONF_NOTIFICATION_FONT): cv.use_id(font.Font),
            cv.Required(CONF_FIRMWARE_UPDATE_TITLE_FONT): cv.use_id(font.Font),
            cv.Required(CONF_FIRMWARE_UPDATE_DETAIL_FONT): cv.use_id(font.Font),
            cv.Optional(CONF_RENDER_METRICS): RENDER_METRICS_SCHEMA,
        }
    ).extend(cv.polling_component_schema("300s")),
    validate_dashboard_config,
    validate_render_schedule,
)


async def _build_text(entry):
    dashboard = cg.new_Pvariable(entry[CONF_ID])
    cg.add(dashboard.set_id(entry[CONF_DASHBOARD_ID]))
    cg.add(
        dashboard.set_frame_interval_ms(entry[CONF_FRAME_INTERVAL].total_milliseconds)
    )
    cg.add(dashboard.set_font(await cg.get_variable(entry[CONF_PANEL_FONT])))
    cg.add(dashboard.set_text(await cg.get_variable(entry[CONF_PANEL_TEXT])))
    return dashboard


async def _build_equalizer(entry):
    entry[CONF_ID].type = EQUALIZER_FACES[entry[CONF_FACE]]
    dashboard = cg.new_Pvariable(entry[CONF_ID])
    cg.add(dashboard.set_id(entry[CONF_DASHBOARD_ID]))
    cg.add(
        dashboard.set_frame_interval_ms(entry[CONF_FRAME_INTERVAL].total_milliseconds)
    )
    return dashboard


async def _build_weather(entry):
    entry[CONF_ID].type = WEATHER_FACES[entry[CONF_FACE]]
    dashboard = cg.new_Pvariable(entry[CONF_ID])
    cg.add(dashboard.set_id(entry[CONF_DASHBOARD_ID]))
    cg.add(
        dashboard.set_frame_interval_ms(entry[CONF_FRAME_INTERVAL].total_milliseconds)
    )
    cg.add(dashboard.set_font_small(await cg.get_variable(entry[CONF_FONT_SMALL])))
    cg.add(dashboard.set_font_big(await cg.get_variable(entry[CONF_FONT_BIG])))
    if CONF_TIME_ID in entry:
        cg.add(dashboard.set_time(await cg.get_variable(entry[CONF_TIME_ID])))
    if CONF_FIXED_TIME in entry:
        cg.add(dashboard.set_fixed_time(entry[CONF_FIXED_TIME]))
    cg.add(dashboard.set_source(await cg.get_variable(entry[CONF_SOURCE])))
    return dashboard


async def _build_clock(entry):
    entry[CONF_ID].type = CLOCK_FACES[entry[CONF_FACE]]
    dashboard = cg.new_Pvariable(entry[CONF_ID])
    cg.add(dashboard.set_id(entry[CONF_DASHBOARD_ID]))
    cg.add(
        dashboard.set_frame_interval_ms(entry[CONF_FRAME_INTERVAL].total_milliseconds)
    )
    if CONF_TIME_ID in entry:
        cg.add(dashboard.set_time(await cg.get_variable(entry[CONF_TIME_ID])))
    if CONF_FIXED_TIME in entry:
        cg.add(dashboard.set_fixed_time(entry[CONF_FIXED_TIME]))
    return dashboard


async def _build_stopwatch(entry):
    dashboard = cg.new_Pvariable(entry[CONF_ID])
    cg.add(dashboard.set_id(entry[CONF_DASHBOARD_ID]))
    cg.add(
        dashboard.set_frame_interval_ms(
            entry[CONF_FRAME_INTERVAL].total_milliseconds
        )
    )
    return dashboard


async def _build_game_of_life(entry):
    dashboard = cg.new_Pvariable(entry[CONF_ID])
    cg.add(dashboard.set_id(entry[CONF_DASHBOARD_ID]))
    cg.add(
        dashboard.set_frame_interval_ms(entry[CONF_FRAME_INTERVAL].total_milliseconds)
    )
    if CONF_SEED in entry:
        cg.add(dashboard.set_seed(entry[CONF_SEED]))
    return dashboard


DASHBOARD_BUILDERS = {
    "text": _build_text,
    "weather": _build_weather,
    "equalizer": _build_equalizer,
    "clock": _build_clock,
    "game_of_life": _build_game_of_life,
    "stopwatch": _build_stopwatch,
}


async def build_controller(config):
    controller = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(controller, config)
    for entry in config[CONF_DASHBOARDS]:
        dashboard = await DASHBOARD_BUILDERS[entry[CONF_PLATFORM]](entry)
        cg.add(controller.add_dashboard(dashboard))
        if entry[CONF_PLATFORM] == "stopwatch":
            cg.add(controller.set_stopwatch_dashboard(dashboard))
    if CONF_NOTIFICATION_FONT in config:
        cg.add(
            controller.set_notification_font(
                await cg.get_variable(config[CONF_NOTIFICATION_FONT])
            )
        )
    cg.add(
        controller.set_firmware_update_title_font(
            await cg.get_variable(config[CONF_FIRMWARE_UPDATE_TITLE_FONT])
        )
    )
    cg.add(
        controller.set_firmware_update_detail_font(
            await cg.get_variable(config[CONF_FIRMWARE_UPDATE_DETAIL_FONT])
        )
    )
    if CONF_RENDER_METRICS in config:
        metrics = config[CONF_RENDER_METRICS]
        cg.add(
            controller.set_render_budget_us(
                metrics[CONF_RENDER_BUDGET].total_microseconds
            )
        )
        average = await sensor.new_sensor(metrics[CONF_RENDER_AVERAGE])
        maximum = await sensor.new_sensor(metrics[CONF_RENDER_MAX])
        over_budget = await sensor.new_sensor(metrics[CONF_RENDER_OVER_BUDGET])
        cg.add(controller.set_render_average_sensor(average))
        cg.add(controller.set_render_max_sensor(maximum))
        cg.add(controller.set_render_over_budget_sensor(over_budget))
    cg.add(controller.set_default_dashboard(config[CONF_DEFAULT_DASHBOARD]))
    return controller


async def to_code(config):
    await build_controller(config)
