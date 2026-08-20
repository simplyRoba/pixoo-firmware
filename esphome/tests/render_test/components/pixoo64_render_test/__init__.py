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
CONF_SNAPSHOTS = "snapshots"
CONF_AT_MS = "at_ms"
CONF_SOURCE_STATE = "source_state"
CONF_PLAYBACK_STATE = "playback_state"
CONF_TITLE = "title"
CONF_ARTIST = "artist"
CONF_DURATION_MS = "duration_ms"
CONF_POSITION_MS = "position_ms"
CONF_MEDIA_IDENTITY = "media_identity"
CONF_ARTWORK_IDENTITY = "artwork_identity"
CONF_ARTWORK_CONTENT_IDENTITY = "artwork_content_identity"
CONF_ARTWORK_CONTENT_REVISION = "artwork_content_revision"
CONF_ARTWORK_AVAILABILITY = "artwork_availability"
CONF_ARTWORK_REVISION = "artwork_revision"
CONF_ARTWORK_COPY_READY_AT_MS = "artwork_copy_ready_at_ms"

pixoo_ns = cg.global_ns.namespace("pixoo")
WeatherCondition = pixoo_ns.enum("WeatherCondition", is_class=True)
WeatherSource = pixoo_ns.class_("WeatherSource")
now_playing_ns = pixoo_ns.namespace("now_playing")
NowPlayingSource = now_playing_ns.class_("NowPlayingSource")
NowPlayingSourceState = now_playing_ns.enum("NowPlayingSourceState", is_class=True)
PlaybackState = now_playing_ns.enum("PlaybackState", is_class=True)
ArtworkAvailability = now_playing_ns.enum("ArtworkAvailability", is_class=True)
render_test_ns = cg.esphome_ns.namespace("pixoo64_render_test")
StaticWeatherSource = render_test_ns.class_("StaticWeatherSource", WeatherSource)
StaticNowPlayingSource = render_test_ns.class_(
    "StaticNowPlayingSource", NowPlayingSource
)

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
SOURCE_STATES = {
    "unconfigured": NowPlayingSourceState.kUnconfigured,
    "waiting": NowPlayingSourceState.kWaiting,
    "no_entity_data": NowPlayingSourceState.kNoEntityData,
    "ready": NowPlayingSourceState.kReady,
    "offline": NowPlayingSourceState.kOffline,
    "stale": NowPlayingSourceState.kStale,
}
PLAYBACK_STATES = {
    "unknown": PlaybackState.kUnknown,
    "playing": PlaybackState.kPlaying,
    "paused": PlaybackState.kPaused,
    "buffering": PlaybackState.kBuffering,
    "idle": PlaybackState.kIdle,
    "on": PlaybackState.kOn,
    "off": PlaybackState.kOff,
    "standby": PlaybackState.kStandby,
    "unavailable": PlaybackState.kUnavailable,
}
ARTWORK_AVAILABILITY = {
    "none": ArtworkAvailability.kNone,
    "pending": ArtworkAvailability.kPending,
    "ready": ArtworkAvailability.kReady,
    "failed": ArtworkAvailability.kFailed,
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


WEATHER_CONFIG_SCHEMA = cv.All(
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

SNAPSHOT_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_AT_MS): cv.int_range(min=0, max=0xFFFFFFFF),
        cv.Required(CONF_SOURCE_STATE): cv.enum(SOURCE_STATES, lower=True),
        cv.Required(CONF_PLAYBACK_STATE): cv.enum(PLAYBACK_STATES, lower=True),
        cv.Optional(CONF_TITLE, default=""): cv.string_strict,
        cv.Optional(CONF_ARTIST, default=""): cv.string_strict,
        cv.Optional(CONF_DURATION_MS): cv.int_range(min=0, max=0xFFFFFFFF),
        cv.Optional(CONF_POSITION_MS): cv.int_range(min=0, max=0xFFFFFFFF),
        cv.Required(CONF_MEDIA_IDENTITY): cv.int_range(
            min=0, max=0xFFFFFFFFFFFFFFFF
        ),
        cv.Optional(CONF_ARTWORK_IDENTITY): cv.int_range(
            min=0, max=0xFFFFFFFFFFFFFFFF
        ),
        cv.Optional(CONF_ARTWORK_CONTENT_IDENTITY): cv.int_range(
            min=0, max=0xFFFFFFFFFFFFFFFF
        ),
        cv.Optional(CONF_ARTWORK_CONTENT_REVISION): cv.int_range(
            min=0, max=0xFFFFFFFF
        ),
        cv.Optional(CONF_ARTWORK_AVAILABILITY, default="none"): cv.enum(
            ARTWORK_AVAILABILITY, lower=True
        ),
        cv.Optional(CONF_ARTWORK_REVISION, default=0): cv.int_range(
            min=0, max=0xFFFFFFFF
        ),
        cv.Optional(CONF_ARTWORK_COPY_READY_AT_MS): cv.int_range(
            min=0, max=0xFFFFFFFF
        ),
    }
)


def validate_now_playing_snapshots(config):
    snapshots = config[CONF_SNAPSHOTS]
    if len(snapshots) > 32:
        raise cv.Invalid("now-playing fixtures support at most 32 snapshots")
    if not snapshots or snapshots[0][CONF_AT_MS] != 0:
        raise cv.Invalid("now-playing fixture snapshots must start at 0ms")
    previous = -1
    for snapshot in snapshots:
        at_ms = snapshot[CONF_AT_MS]
        if at_ms <= previous:
            raise cv.Invalid("now-playing fixture snapshots must be strictly ordered")
        previous = at_ms
        has_artwork = CONF_ARTWORK_IDENTITY in snapshot
        availability = snapshot[CONF_ARTWORK_AVAILABILITY]
        if has_artwork != (availability != "none"):
            raise cv.Invalid(
                "artwork_identity and non-none artwork_availability must appear together"
            )
        if has_artwork and snapshot[CONF_ARTWORK_REVISION] == 0:
            raise cv.Invalid("artwork fixtures require a nonzero artwork_revision")
        has_content_identity = CONF_ARTWORK_CONTENT_IDENTITY in snapshot
        has_content_revision = CONF_ARTWORK_CONTENT_REVISION in snapshot
        if not has_artwork and has_content_identity:
            raise cv.Invalid("artwork_content_identity requires artwork_identity")
        if has_content_identity != has_content_revision:
            raise cv.Invalid(
                "artwork_content_identity and artwork_content_revision must appear together"
            )
    return config


NOW_PLAYING_CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.declare_id(StaticNowPlayingSource),
            cv.Required(CONF_SNAPSHOTS): cv.ensure_list(SNAPSHOT_SCHEMA),
        }
    ),
    validate_now_playing_snapshots,
)

def validate_fixture(config):
    if CONF_SNAPSHOTS in config:
        return NOW_PLAYING_CONFIG_SCHEMA(config)
    return WEATHER_CONFIG_SCHEMA(config)


CONFIG_SCHEMA = validate_fixture


async def to_code(config):
    if CONF_SNAPSHOTS in config:
        var = cg.new_Pvariable(config[CONF_ID])
        for snapshot in config[CONF_SNAPSHOTS]:
            has_duration = CONF_DURATION_MS in snapshot
            has_position = CONF_POSITION_MS in snapshot
            has_artwork = CONF_ARTWORK_IDENTITY in snapshot
            cg.add(
                var.add_snapshot(
                    snapshot[CONF_AT_MS],
                    snapshot[CONF_SOURCE_STATE],
                    snapshot[CONF_PLAYBACK_STATE],
                    snapshot[CONF_TITLE],
                    snapshot[CONF_ARTIST],
                    has_duration,
                    snapshot.get(CONF_DURATION_MS, 0),
                    has_position,
                    snapshot.get(CONF_POSITION_MS, 0),
                    snapshot[CONF_MEDIA_IDENTITY],
                    has_artwork,
                    snapshot.get(CONF_ARTWORK_IDENTITY, 0),
                    snapshot[CONF_ARTWORK_AVAILABILITY],
                    snapshot[CONF_ARTWORK_REVISION],
                    snapshot.get(
                        CONF_ARTWORK_COPY_READY_AT_MS, snapshot[CONF_AT_MS]
                    ),
                    CONF_ARTWORK_CONTENT_IDENTITY in snapshot,
                    snapshot.get(CONF_ARTWORK_CONTENT_IDENTITY, 0),
                    snapshot.get(CONF_ARTWORK_CONTENT_REVISION, 0),
                )
            )
        return var

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
