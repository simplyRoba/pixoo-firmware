import esphome.codegen as cg
from esphome import automation, pins
from esphome.components import (
    api,
    http_request,
    light,
    number,
    output,
    rtttl,
    select,
    sensor,
    switch,
    time,
)
from esphome.components.i2s_audio import microphone as i2s_audio_microphone
from esphome.components import pixoo64_content as content
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import (
    CONF_BITS_PER_SAMPLE,
    CONF_CHANNEL,
    CONF_FREQUENCY,
    CONF_ID,
    CONF_INVERTED,
    CONF_LATITUDE,
    CONF_LONGITUDE,
    CONF_MAX_POWER,
    CONF_INITIAL_OPTION,
    CONF_MIN_POWER,
    CONF_NUMBER,
    CONF_OPTIONS,
    CONF_PIN,
    CONF_PLATFORM,
    CONF_SAMPLE_RATE,
    CONF_UPDATE_INTERVAL,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_NONE,
    SCHEDULER_DONT_RUN,
    UNIT_MILLISECOND,
)

CONF_PANEL = "panel"
CONF_PANEL_POWER_OUTPUT = "power_output"
CONF_PANEL_SPI_MOSI_PIN = "spi_mosi_pin"
CONF_PANEL_SPI_SCLK_PIN = "spi_sclk_pin"
CONF_PANEL_SPI_CS_PIN = "spi_cs_pin"
CONF_PANEL_UART_RX_PIN = "uart_rx_pin"
CONF_PANEL_SPI_CLOCK_HZ = "spi_clock_hz"
CONF_PANEL_UART_BAUD = "uart_baud"
CONF_PANEL_SOFT_START_DURATION = "soft_start_duration"
CONF_PANEL_SOFT_START_PERIOD = "soft_start_period"
CONF_RENDERER = "renderer"
CONF_WEATHER = "weather"
CONF_NOW_PLAYING = "now_playing"
CONF_HTTP_GATE = "http_gate"
CONF_API_SERVER = "api_server"
CONF_CLOCK = "clock"
CONF_ENTITY_ID = "entity_id"
CONF_HOME_ASSISTANT_URL = "home_assistant_url"
CONF_REFRESH_INTERVAL = "refresh_interval"
CONF_SOUND_RTTTL = "rtttl"
CONF_SOUND_ENABLE_SWITCH = "enable_switch"
CONF_MICROPHONE = "microphone"
CONF_MICROPHONE_ID = "microphone_id"
CONF_MICROPHONE_ENABLE_SWITCH = "enable_switch"
CONF_LIGHT = "light"
CONF_DASHBOARD_SELECT = "dashboard_select"
CONF_TEXT = "text"
CONF_TITLE = "title"
CONF_REACTION = "reaction"
CONF_SEVERITY = "severity"
CONF_DURATION = "duration"
CONF_DURATION_MS = "duration_ms"
CONF_SOUND = "sound"
CONF_USE_APLL = "use_apll"
CONF_FRAME_METRICS = "frame_metrics"
CONF_FRAME_METRICS_WINDOW = "window"
CONF_FRAME_AVERAGE = "average"
CONF_FRAME_MAX = "maximum"
CONF_RENDERED_FPS = "rendered_fps"

SEVERITIES = ["info", "success", "warning", "error"]
REACTIONS = [
    "laughing",
    "love",
    "crying",
    "angry",
    "poop",
    "approve",
    "disapprove",
    "celebrate",
    "thinking",
    "surprised",
    "fire",
    "eyes",
]
SOUNDS = [
    "chirp",
    "success",
    "pling1",
    "pling2",
    "pling3",
    "pling4",
    "alarm1",
    "alarm2",
    "alarm3",
]

AUTO_LOAD = [
    "display",
    "light",
    "microphone",
    "rtttl",
    "select",
    "sensor",
    "switch",
]

pixoo_ns = cg.global_ns.namespace("pixoo")
pixoo64_ns = cg.esphome_ns.namespace("pixoo64")
PanelPort = pixoo_ns.class_("PanelPort")
RenderPort = pixoo_ns.class_("RenderPort")
SoundPlayer = pixoo_ns.class_("SoundPlayer")
MicrophonePort = pixoo_ns.class_("MicrophonePort")
I2SAudioMicrophone = i2s_audio_microphone.I2SAudioMicrophone
LightStateSink = pixoo_ns.class_("LightStateSink")
SystemPort = pixoo_ns.class_("SystemPort")
FrameMetricsPort = pixoo_ns.class_("FrameMetricsPort")
WeatherSource = pixoo_ns.class_("WeatherSource")
adapters_ns = pixoo64_ns.namespace("adapters")
OpenMeteoSource = adapters_ns.class_("OpenMeteoSource", cg.Component, WeatherSource)
HttpRequestGate = adapters_ns.class_("HttpRequestGate", cg.Component)
NowPlayingSource = pixoo_ns.namespace("now_playing").class_("NowPlayingSource")
HomeAssistantMediaSource = adapters_ns.class_(
    "HomeAssistantMediaSource", cg.Component, NowPlayingSource
)
MicrophoneAdapter = adapters_ns.class_(
    "MicrophoneAdapter", cg.PollingComponent, MicrophonePort
)
RtttlSoundPlayer = adapters_ns.class_(
    "RtttlSoundPlayer", cg.Component, SoundPlayer
)
Pixoo64Panel = pixoo64_ns.class_("Pixoo64Panel", cg.Component, PanelPort)
FirmwareAppComponent = pixoo64_ns.class_(
    "FirmwareAppComponent",
    cg.PollingComponent,
    LightStateSink,
    SystemPort,
    FrameMetricsPort,
)
ShowNotificationAction = pixoo64_ns.class_(
    "ShowNotificationAction", automation.Action
)
ShowReactionAction = pixoo64_ns.class_("ShowReactionAction", automation.Action)
ClearOverlayQueueAction = pixoo64_ns.class_(
    "ClearOverlayQueueAction", automation.Action
)
BeginFirmwareUpdateAction = pixoo64_ns.class_(
    "BeginFirmwareUpdateAction", automation.Action
)
RebootAction = pixoo64_ns.class_("RebootAction", automation.Action)
StopwatchStartAction = pixoo64_ns.class_(
    "StopwatchStartAction", automation.Action
)
StopwatchStopAction = pixoo64_ns.class_(
    "StopwatchStopAction", automation.Action
)
StopwatchResetAction = pixoo64_ns.class_(
    "StopwatchResetAction", automation.Action
)
TimerSetAction = pixoo64_ns.class_("TimerSetAction", automation.Action)
TimerStartAction = pixoo64_ns.class_("TimerStartAction", automation.Action)
TimerStopAction = pixoo64_ns.class_("TimerStopAction", automation.Action)
TimerResetAction = pixoo64_ns.class_("TimerResetAction", automation.Action)
NowPlayingConfigureAction = adapters_ns.class_(
    "NowPlayingConfigureAction", automation.Action
)
NowPlayingClearAction = adapters_ns.class_("NowPlayingClearAction", automation.Action)


def validate_panel_timing(config):
    duration_ms = config[CONF_PANEL_SOFT_START_DURATION].total_milliseconds
    period_ms = config[CONF_PANEL_SOFT_START_PERIOD].total_milliseconds
    if duration_ms <= 0 or period_ms <= 0:
        raise cv.Invalid("soft-start duration and period must be positive")
    if duration_ms > 1_000:
        raise cv.Invalid("soft-start duration must not exceed 1 second")
    if period_ms > 100:
        raise cv.Invalid("soft-start period must not exceed 100 milliseconds")
    if duration_ms < period_ms:
        raise cv.Invalid("soft-start duration must be at least its period")
    return config


PANEL_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Pixoo64Panel),
            cv.Required(CONF_PANEL_POWER_OUTPUT): cv.use_id(output.FloatOutput),
            cv.Required(CONF_PANEL_SPI_MOSI_PIN): pins.internal_gpio_output_pin_number,
            cv.Required(CONF_PANEL_SPI_SCLK_PIN): pins.internal_gpio_output_pin_number,
            cv.Required(CONF_PANEL_SPI_CS_PIN): pins.internal_gpio_output_pin_number,
            cv.Required(CONF_PANEL_UART_RX_PIN): pins.internal_gpio_input_pin_number,
            cv.Required(CONF_PANEL_SPI_CLOCK_HZ): cv.int_range(
                min=1, max=80_000_000
            ),
            cv.Required(CONF_PANEL_UART_BAUD): cv.int_range(min=1, max=5_000_000),
            cv.Required(
                CONF_PANEL_SOFT_START_DURATION
            ): cv.positive_time_period_milliseconds,
            cv.Required(
                CONF_PANEL_SOFT_START_PERIOD
            ): cv.positive_time_period_milliseconds,
        }
    ),
    validate_panel_timing,
)

MICROPHONE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MicrophoneAdapter),
        cv.Required(CONF_MICROPHONE_ID): cv.use_id(I2SAudioMicrophone),
        cv.Required(CONF_MICROPHONE_ENABLE_SWITCH): cv.use_id(switch.Switch),
    }
).extend(cv.polling_component_schema("33ms"))

SOUND_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RtttlSoundPlayer),
        cv.Required(CONF_SOUND_RTTTL): cv.use_id(rtttl.Rtttl),
        cv.Optional(CONF_SOUND_ENABLE_SWITCH): cv.use_id(switch.Switch),
    }
).extend(cv.COMPONENT_SCHEMA)

def validate_frame_metrics(config):
    if config[CONF_FRAME_METRICS_WINDOW].total_milliseconds > 0x7FFFFFFF:
        raise cv.Invalid(
            "frame metrics window must not exceed 2147483647 milliseconds"
        )
    return config


FRAME_METRICS_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(
                CONF_FRAME_METRICS_WINDOW
            ): cv.positive_time_period_milliseconds,
            cv.Required(CONF_FRAME_AVERAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_MILLISECOND,
                accuracy_decimals=2,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                state_class=STATE_CLASS_NONE,
            ),
            cv.Required(CONF_FRAME_MAX): sensor.sensor_schema(
                unit_of_measurement=UNIT_MILLISECOND,
                accuracy_decimals=2,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                state_class=STATE_CLASS_NONE,
            ),
            cv.Required(CONF_RENDERED_FPS): sensor.sensor_schema(
                unit_of_measurement="fps",
                accuracy_decimals=2,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                state_class=STATE_CLASS_NONE,
            ),
        }
    ),
    validate_frame_metrics,
)


def validate_frame_metrics_schedule(config):
    if (
        CONF_FRAME_METRICS in config
        and config[CONF_UPDATE_INTERVAL].total_milliseconds == SCHEDULER_DONT_RUN
    ):
        raise cv.Invalid("frame metrics require a finite update_interval")
    return config


NOW_PLAYING_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HomeAssistantMediaSource),
        cv.Required(CONF_API_SERVER): cv.use_id(api.APIServer),
        cv.Required(http_request.CONF_HTTP_REQUEST_ID): cv.use_id(
            http_request.HttpRequestComponent
        ),
        cv.Required(CONF_HTTP_GATE): cv.use_id(HttpRequestGate),
        cv.Optional(CONF_CLOCK): cv.use_id(time.RealTimeClock),
    }
).extend(cv.COMPONENT_SCHEMA)

HTTP_GATE_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(HttpRequestGate)})

WEATHER_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_PLATFORM): cv.one_of("open_meteo", lower=True),
        cv.Required(CONF_ID): cv.declare_id(OpenMeteoSource),
        cv.Required(http_request.CONF_HTTP_REQUEST_ID): cv.use_id(
            http_request.HttpRequestComponent
        ),
        cv.Required(CONF_HTTP_GATE): cv.use_id(HttpRequestGate),
        cv.Required(CONF_LATITUDE): cv.use_id(number.Number),
        cv.Required(CONF_LONGITUDE): cv.use_id(number.Number),
        cv.Required(CONF_REFRESH_INTERVAL): cv.use_id(number.Number),
    }
).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(FirmwareAppComponent),
        cv.Required(CONF_PANEL): PANEL_SCHEMA,
        cv.Required(CONF_HTTP_GATE): HTTP_GATE_SCHEMA,
        cv.Required(CONF_RENDERER): cv.use_id(RenderPort),
        cv.Optional(CONF_WEATHER): WEATHER_SCHEMA,
        cv.Optional(CONF_NOW_PLAYING): NOW_PLAYING_SCHEMA,
        cv.Optional(CONF_SOUND): SOUND_SCHEMA,
        cv.Optional(CONF_MICROPHONE): MICROPHONE_SCHEMA,
        cv.Required(CONF_LIGHT): cv.use_id(light.LightState),
        cv.Required(CONF_DASHBOARD_SELECT): cv.use_id(select.Select),
        cv.Optional(CONF_FRAME_METRICS): FRAME_METRICS_SCHEMA,
    }
).extend(cv.polling_component_schema("33ms"))

CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_frame_metrics_schedule)

SHOW_NOTIFICATION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(FirmwareAppComponent),
        cv.Required(CONF_TEXT): cv.templatable(cv.string),
        cv.Optional(CONF_TITLE, default=""): cv.templatable(cv.string),
        cv.Optional(CONF_SEVERITY, default="info"): cv.templatable(
            cv.one_of(*SEVERITIES, lower=True)
        ),
        cv.Optional(CONF_DURATION, default=0): cv.templatable(cv.int_),
        cv.Optional(CONF_SOUND, default=""): cv.templatable(
            cv.one_of("", *SOUNDS, lower=True)
        ),
    }
)

SHOW_REACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(FirmwareAppComponent),
        cv.Required(CONF_REACTION): cv.templatable(
            cv.one_of(*REACTIONS, lower=True)
        ),
    }
)

CLEAR_OVERLAY_QUEUE_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(): cv.use_id(FirmwareAppComponent)}
)

BEGIN_FIRMWARE_UPDATE_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(): cv.use_id(FirmwareAppComponent)}
)
REBOOT_ACTION_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(): cv.use_id(FirmwareAppComponent)}
)
STOPWATCH_ACTION_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(): cv.use_id(FirmwareAppComponent)}
)
TIMER_SET_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(FirmwareAppComponent),
        cv.Required(CONF_DURATION_MS): cv.templatable(cv.int_),
    }
)
TIMER_ACTION_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(): cv.use_id(FirmwareAppComponent)}
)
NOW_PLAYING_CONFIGURE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(HomeAssistantMediaSource),
        cv.Required(CONF_ENTITY_ID): cv.templatable(cv.string),
        cv.Required(CONF_HOME_ASSISTANT_URL): cv.templatable(cv.string),
    }
)
NOW_PLAYING_CLEAR_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(): cv.use_id(HomeAssistantMediaSource)}
)


@automation.register_action(
    "pixoo64.show_notification",
    ShowNotificationAction,
    SHOW_NOTIFICATION_SCHEMA,
    synchronous=True,
)
async def show_notification_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    templ = await cg.templatable(config[CONF_TEXT], args, cg.std_string)
    cg.add(var.set_text(templ))
    templ = await cg.templatable(config[CONF_TITLE], args, cg.std_string)
    cg.add(var.set_title(templ))
    templ = await cg.templatable(config[CONF_SEVERITY], args, cg.std_string)
    cg.add(var.set_severity(templ))
    templ = await cg.templatable(config[CONF_DURATION], args, cg.int32)
    cg.add(var.set_duration(templ))
    templ = await cg.templatable(config[CONF_SOUND], args, cg.std_string)
    cg.add(var.set_sound(templ))
    return var


@automation.register_action(
    "pixoo64.show_reaction",
    ShowReactionAction,
    SHOW_REACTION_SCHEMA,
    synchronous=True,
)
async def show_reaction_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    templ = await cg.templatable(config[CONF_REACTION], args, cg.std_string)
    cg.add(var.set_reaction(templ))
    return var


@automation.register_action(
    "pixoo64.clear_overlay_queue",
    ClearOverlayQueueAction,
    CLEAR_OVERLAY_QUEUE_SCHEMA,
    synchronous=True,
)
async def clear_overlay_queue_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "pixoo64.stopwatch_start",
    StopwatchStartAction,
    STOPWATCH_ACTION_SCHEMA,
    synchronous=True,
)
async def stopwatch_start_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "pixoo64.stopwatch_stop",
    StopwatchStopAction,
    STOPWATCH_ACTION_SCHEMA,
    synchronous=True,
)
async def stopwatch_stop_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "pixoo64.stopwatch_reset",
    StopwatchResetAction,
    STOPWATCH_ACTION_SCHEMA,
    synchronous=True,
)
async def stopwatch_reset_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "pixoo64.timer_set",
    TimerSetAction,
    TIMER_SET_ACTION_SCHEMA,
    synchronous=True,
)
async def timer_set_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    templ = await cg.templatable(config[CONF_DURATION_MS], args, cg.int32)
    cg.add(var.set_duration_ms(templ))
    return var


@automation.register_action(
    "pixoo64.timer_start",
    TimerStartAction,
    TIMER_ACTION_SCHEMA,
    synchronous=True,
)
async def timer_start_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "pixoo64.timer_stop",
    TimerStopAction,
    TIMER_ACTION_SCHEMA,
    synchronous=True,
)
async def timer_stop_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "pixoo64.timer_reset",
    TimerResetAction,
    TIMER_ACTION_SCHEMA,
    synchronous=True,
)
async def timer_reset_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "pixoo64.now_playing_configure",
    NowPlayingConfigureAction,
    NOW_PLAYING_CONFIGURE_SCHEMA,
    synchronous=True,
)
async def now_playing_configure_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    templ = await cg.templatable(config[CONF_ENTITY_ID], args, cg.std_string)
    cg.add(var.set_entity_id(templ))
    templ = await cg.templatable(config[CONF_HOME_ASSISTANT_URL], args, cg.std_string)
    cg.add(var.set_home_assistant_url(templ))
    return var


@automation.register_action(
    "pixoo64.now_playing_clear",
    NowPlayingClearAction,
    NOW_PLAYING_CLEAR_SCHEMA,
    synchronous=True,
)
async def now_playing_clear_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "pixoo64.reboot",
    RebootAction,
    REBOOT_ACTION_SCHEMA,
    synchronous=True,
)
async def reboot_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "pixoo64.begin_firmware_update",
    BeginFirmwareUpdateAction,
    BEGIN_FIRMWARE_UPDATE_SCHEMA,
    synchronous=True,
)
async def begin_firmware_update_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


def validate_panel_power_output(config):
    full_config = fv.full_config.get()
    output_path = full_config.get_path_for_id(
        config[CONF_PANEL][CONF_PANEL_POWER_OUTPUT]
    )[:-1]
    output_config = full_config.get_config_for_path(output_path)
    pin_config = output_config.get(CONF_PIN, {})
    if (
        output_config.get(CONF_PLATFORM) != "ledc"
        or pin_config.get(CONF_NUMBER) != 22
        or output_config.get(CONF_CHANNEL) != 8
        or output_config.get(CONF_FREQUENCY) != 1000.0
        or output_config.get(CONF_INVERTED, False)
        or pin_config.get(CONF_INVERTED, False)
        or output_config.get(CONF_MIN_POWER, 0.0) != 0.0
        or output_config.get(CONF_MAX_POWER, 1.0) != 1.0
    ):
        raise cv.Invalid(
            "panel power_output must be an uninverted, full-range LEDC output "
            "on GPIO22, channel 8, at 1kHz"
        )
    return config


def _selected_content_renderer(config):
    full_config = fv.full_config.get()
    renderer_path = full_config.get_path_for_id(config[CONF_RENDERER])[:-1]
    if not renderer_path or renderer_path[0] != "pixoo64_content":
        return None
    return full_config.get_config_for_path(renderer_path)


def validate_renderer_wiring(config):
    renderer_config = _selected_content_renderer(config)
    if renderer_config is None:
        return config

    equalizer_ids = {
        entry[CONF_ID]
        for entry in renderer_config[content.CONF_DASHBOARDS]
        if entry[content.CONF_PLATFORM] == "equalizer"
    }
    if not equalizer_ids:
        return config
    if CONF_MICROPHONE not in config:
        raise cv.Invalid("microphone wiring is required for an equalizer dashboard")
    return config


def validate_dashboard_select(config):
    renderer_config = _selected_content_renderer(config)
    if renderer_config is None:
        return config

    full_config = fv.full_config.get()
    select_path = full_config.get_path_for_id(config[CONF_DASHBOARD_SELECT])[:-1]
    select_config = full_config.get_config_for_path(select_path)
    dashboard_ids = {
        entry[content.CONF_DASHBOARD_ID]
        for entry in renderer_config[content.CONF_DASHBOARDS]
    }
    select_options = set(select_config.get(CONF_OPTIONS, []))
    if dashboard_ids != select_options:
        missing = sorted(dashboard_ids - select_options)
        stale = sorted(select_options - dashboard_ids)
        raise cv.Invalid(
            "dashboard select options must exactly match the selected renderer "
            f"dashboard IDs (missing: {missing}, stale: {stale})"
        )

    if select_config.get(CONF_INITIAL_OPTION) != renderer_config[
        content.CONF_DEFAULT_DASHBOARD
    ]:
        raise cv.Invalid(
            "dashboard select initial_option must match the renderer "
            "default_dashboard"
        )
    return config


def validate_microphone_source(config):
    if CONF_MICROPHONE not in config:
        return config

    full_config = fv.full_config.get()
    microphone_config_ref = config[CONF_MICROPHONE]
    microphone_path = full_config.get_path_for_id(
        microphone_config_ref[CONF_MICROPHONE_ID]
    )[:-1]
    microphone_config = full_config.get_config_for_path(microphone_path)
    if (
        microphone_config.get(CONF_PLATFORM) != "i2s_audio"
        or microphone_config.get(CONF_SAMPLE_RATE) != 32000
        or microphone_config.get(CONF_BITS_PER_SAMPLE) != 32
        or microphone_config.get(CONF_CHANNEL) != "left"
        or not microphone_config.get(CONF_USE_APLL, False)
    ):
        raise cv.Invalid(
            "microphone source must be I2S audio at 32kHz, 32-bit, left "
            "channel, with APLL enabled"
        )
    return config


def validate_now_playing_renderer(config):
    renderer_config = _selected_content_renderer(config)
    if renderer_config is None:
        return config
    entries = [
        entry
        for entry in renderer_config[content.CONF_DASHBOARDS]
        if entry[content.CONF_PLATFORM] == "now_playing"
    ]
    if CONF_NOW_PLAYING not in config:
        if entries:
            raise cv.Invalid(
                "a now_playing dashboard requires the pixoo64 now_playing source"
            )
        return config
    if len(entries) != 1 or entries[0][content.CONF_DASHBOARD_ID] != "now_playing":
        raise cv.Invalid(
            "the configured source requires one dashboard_id: now_playing"
        )
    if entries[0][content.CONF_SOURCE] != config[CONF_NOW_PLAYING][CONF_ID]:
        raise cv.Invalid(
            "the now_playing dashboard must reference the configured pixoo64 source"
        )
    return config


def validate_now_playing_api(config):
    if CONF_NOW_PLAYING not in config:
        return config
    full_config = fv.full_config.get()
    api_path = full_config.get_path_for_id(
        config[CONF_NOW_PLAYING][CONF_API_SERVER]
    )[:-1]
    api_config = full_config.get_config_for_path(api_path)
    if not api_path or api_path[0] != "api" or not api_config.get(
        api.CONF_HOMEASSISTANT_STATES, False
    ):
        raise cv.Invalid(
            "now_playing api_server must select an api with homeassistant_states: true"
        )
    return config


def validate_shared_http_wiring(config):
    # Every configured HTTP-backed source names one top-level transport and the
    # one codegen-owned gate. Either source remains independently optional.
    sources = [
        config[name]
        for name in (CONF_WEATHER, CONF_NOW_PLAYING)
        if name in config
    ]
    if not sources:
        return config
    gate = config[CONF_HTTP_GATE][CONF_ID]
    if gate.id != "panel_http_gate":
        raise cv.Invalid("shared http_gate must be named panel_http_gate")
    if any(source[CONF_HTTP_GATE] != gate for source in sources):
        raise cv.Invalid("HTTP-backed sources must use the one shared http_gate")
    transport_id = sources[0][http_request.CONF_HTTP_REQUEST_ID]
    if transport_id.id != "panel_http" or any(
        source[http_request.CONF_HTTP_REQUEST_ID] != transport_id
        for source in sources[1:]
    ):
        raise cv.Invalid("HTTP-backed sources must use the one shared panel_http")
    full_config = fv.full_config.get()
    transport_path = full_config.get_path_for_id(transport_id)[:-1]
    if not transport_path or transport_path[0] != "http_request":
        raise cv.Invalid("shared transport must be the top-level http_request")
    transport = full_config.get_config_for_path(transport_path)
    if (transport.get("timeout").total_milliseconds != 5000 or
            transport.get(http_request.CONF_FOLLOW_REDIRECTS) is not False or
            transport.get(http_request.CONF_VERIFY_SSL) is not False):
        raise cv.Invalid("shared panel_http must use timeout 5s, follow_redirects false, and verify_ssl false")
    return config


def validate_now_playing_actions(config):
    if CONF_NOW_PLAYING not in config:
        return config
    full_config = fv.full_config.get()
    api_path = full_config.get_path_for_id(
        config[CONF_NOW_PLAYING][CONF_API_SERVER]
    )[:-1]
    api_config = full_config.get_config_for_path(api_path)
    action_names = {
        action.get(api.CONF_ACTION)
        for action in api_config.get(api.CONF_ACTIONS, [])
    }
    required = {"now_playing_configure", "now_playing_clear"}
    if not required <= action_names:
        raise cv.Invalid("now_playing requires configure and clear API actions")
    return config


def validate_button_inputs(config):
    full_config = fv.full_config.get()
    binary_sensors = full_config.get_config_for_path(["binary_sensor"])
    for pin_number in (34, 35):
        matches = [
            sensor
            for sensor in binary_sensors
            if sensor.get(CONF_PLATFORM) == "gpio"
            and sensor.get(CONF_PIN, {}).get(CONF_NUMBER) == pin_number
        ]
        if len(matches) != 1 or not matches[0][CONF_PIN].get(CONF_INVERTED, False):
            raise cv.Invalid(
                f"GPIO{pin_number} panel button must be configured active-low"
            )
    return config


FINAL_VALIDATE_SCHEMA = cv.All(
    validate_panel_power_output,
    validate_renderer_wiring,
    validate_now_playing_renderer,
    validate_dashboard_select,
    validate_microphone_source,
    validate_now_playing_api,
    validate_shared_http_wiring,
    validate_now_playing_actions,
    validate_button_inputs,
)


async def to_code(config):
    cg.add_global(
        cg.RawStatement('#include "esphome/components/pixoo64/http_request_gate.h"')
    )
    http_gate = cg.new_Pvariable(config[CONF_HTTP_GATE][CONF_ID])
    await cg.register_component(http_gate, config[CONF_HTTP_GATE])

    panel_config = config[CONF_PANEL]
    panel = cg.new_Pvariable(panel_config[CONF_ID])
    await cg.register_component(panel, panel_config)
    cg.add(
        panel.set_power_output(
            await cg.get_variable(panel_config[CONF_PANEL_POWER_OUTPUT])
        )
    )
    cg.add(panel.set_spi_mosi_pin(panel_config[CONF_PANEL_SPI_MOSI_PIN]))
    cg.add(panel.set_spi_sclk_pin(panel_config[CONF_PANEL_SPI_SCLK_PIN]))
    cg.add(panel.set_spi_cs_pin(panel_config[CONF_PANEL_SPI_CS_PIN]))
    cg.add(panel.set_uart_rx_pin(panel_config[CONF_PANEL_UART_RX_PIN]))
    cg.add(panel.set_spi_clock_hz(panel_config[CONF_PANEL_SPI_CLOCK_HZ]))
    cg.add(panel.set_uart_baud(panel_config[CONF_PANEL_UART_BAUD]))
    cg.add(
        panel.set_soft_start_duration_ms(
            panel_config[CONF_PANEL_SOFT_START_DURATION].total_milliseconds
        )
    )
    cg.add(
        panel.set_soft_start_period_ms(
            panel_config[CONF_PANEL_SOFT_START_PERIOD].total_milliseconds
        )
    )

    if CONF_WEATHER in config:
        cg.add_global(
            cg.RawStatement('#include "esphome/components/pixoo64/open_meteo.h"')
        )
        weather_config = config[CONF_WEATHER]
        weather = cg.new_Pvariable(weather_config[CONF_ID])
        await cg.register_component(weather, weather_config)
        cg.add(
            weather.set_http_request(
                await cg.get_variable(weather_config[http_request.CONF_HTTP_REQUEST_ID])
            )
        )
        cg.add(weather.set_http_request_gate(http_gate))
        cg.add(weather.set_latitude(await cg.get_variable(weather_config[CONF_LATITUDE])))
        cg.add(weather.set_longitude(await cg.get_variable(weather_config[CONF_LONGITUDE])))
        cg.add(
            weather.set_refresh_interval(
                await cg.get_variable(weather_config[CONF_REFRESH_INTERVAL])
            )
        )

    if CONF_NOW_PLAYING in config:
        cg.add_build_flag("-DUSE_PIXOO64_NOW_PLAYING")
        # The project-owned worker decoder uses these libraries directly. It
        # deliberately does not auto-load or instantiate runtime_image wrappers.
        cg.add_library(
            "JPEGDEC",
            "1.8.4",
            "https://github.com/bitbank2/JPEGDEC#1.8.4",
        )
        cg.add_library("pngle", "1.1.0")
        cg.add_build_flag("-DPNGLE_NO_GAMMA_CORRECTION")
        # pngle has no allocator hook. artwork_decoder scopes this linker wrap
        # to its worker task so all pngle scratch remains PSRAM-only.
        cg.add_build_flag("-Wl,--wrap=calloc")
        cg.add_global(
            cg.RawStatement(
                '#include "esphome/components/pixoo64/home_assistant_media_source.h"'
            )
        )
        now_playing_config = config[CONF_NOW_PLAYING]
        now_playing = cg.new_Pvariable(now_playing_config[CONF_ID])
        await cg.register_component(now_playing, now_playing_config)
        cg.add(
            now_playing.set_api_server(
                await cg.get_variable(now_playing_config[CONF_API_SERVER])
            )
        )
        cg.add(
            now_playing.set_http_request(
                await cg.get_variable(now_playing_config[http_request.CONF_HTTP_REQUEST_ID])
            )
        )
        cg.add(now_playing.set_http_request_gate(http_gate))
        if CONF_CLOCK in now_playing_config:
            cg.add(
                now_playing.set_clock(
                    await cg.get_variable(now_playing_config[CONF_CLOCK])
                )
            )

    renderer = await cg.get_variable(config[CONF_RENDERER])

    microphone_adapter = None
    if CONF_MICROPHONE in config:
        cg.add_global(
            cg.RawStatement(
                '#include "esphome/components/pixoo64/microphone_adapter.h"'
            )
        )
        microphone_config = config[CONF_MICROPHONE]
        microphone_adapter = cg.new_Pvariable(microphone_config[CONF_ID])
        await cg.register_component(microphone_adapter, microphone_config)
        cg.add(
            microphone_adapter.set_microphone(
                await cg.get_variable(microphone_config[CONF_MICROPHONE_ID])
            )
        )
        cg.add(
            microphone_adapter.set_enable_switch(
                await cg.get_variable(
                    microphone_config[CONF_MICROPHONE_ENABLE_SWITCH]
                )
            )
        )
        cg.add(
            microphone_adapter.set_levels_sink(
                await cg.get_variable(config[CONF_RENDERER])
            )
        )

    sound_player = None
    if CONF_SOUND in config:
        cg.add_global(
            cg.RawStatement(
                '#include "esphome/components/pixoo64/rtttl_sound_player.h"'
            )
        )
        sound_config = config[CONF_SOUND]
        sound_player = cg.new_Pvariable(sound_config[CONF_ID])
        await cg.register_component(sound_player, sound_config)
        cg.add(
            sound_player.set_rtttl(
                await cg.get_variable(sound_config[CONF_SOUND_RTTTL])
            )
        )
        if CONF_SOUND_ENABLE_SWITCH in sound_config:
            cg.add(
                sound_player.set_enable_switch(
                    await cg.get_variable(sound_config[CONF_SOUND_ENABLE_SWITCH])
                )
            )

    app = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(app, config)
    cg.add(app.set_panel(panel))
    cg.add(app.set_panel_component(panel))
    cg.add(app.set_renderer(renderer))
    if CONF_FRAME_METRICS in config:
        frame_metrics = config[CONF_FRAME_METRICS]
        cg.add(
            app.set_frame_metrics_window_ms(
                frame_metrics[CONF_FRAME_METRICS_WINDOW].total_milliseconds
            )
        )
        cg.add(
            app.set_frame_average_sensor(
                await sensor.new_sensor(frame_metrics[CONF_FRAME_AVERAGE])
            )
        )
        cg.add(
            app.set_frame_max_sensor(
                await sensor.new_sensor(frame_metrics[CONF_FRAME_MAX])
            )
        )
        cg.add(
            app.set_rendered_fps_sensor(
                await sensor.new_sensor(frame_metrics[CONF_RENDERED_FPS])
            )
        )
    cg.add(app.set_light(await cg.get_variable(config[CONF_LIGHT])))
    cg.add(
        app.set_dashboard_select(
            await cg.get_variable(config[CONF_DASHBOARD_SELECT])
        )
    )
    if CONF_SOUND in config:
        cg.add(app.set_sound_player(sound_player))
    if microphone_adapter is not None:
        cg.add(app.set_microphone(microphone_adapter))
