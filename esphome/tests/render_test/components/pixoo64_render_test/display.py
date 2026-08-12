import esphome.codegen as cg
from esphome.components import display, text
import esphome.config_validation as cv
from esphome.const import CONF_ID

CONF_CONTENT_CONTROLLER = "content_controller"
CONF_OUTPUT_DIR = "output_dir"
CONF_EQUALIZER = "equalizer"
CONF_PANEL_TEXT = "panel_text"
CONF_ANIMATION_FRAMES = "animation_frames"
CONF_DASHBOARD = "dashboard"
CONF_NOW_MS = "now_ms"
CONF_SNAPSHOT = "snapshot"
CONF_BASE_VISIBLE = "base_visible"
CONF_STOPWATCH_ELAPSED_MS = "stopwatch_elapsed_ms"
CONF_STOPWATCH_RUNNING = "stopwatch_running"

pixoo64_ns = cg.esphome_ns.namespace("pixoo64")
content_ns = pixoo64_ns.namespace("content")
dashboard_ns = pixoo64_ns.namespace("dashboard")
render_test_ns = cg.esphome_ns.namespace("pixoo64_render_test")
ContentController = content_ns.class_("ContentController")
EqualizerDashboard = dashboard_ns.class_("EqualizerDashboard")
RenderTestDisplay = render_test_ns.class_("RenderTestDisplay", display.Display)

# A dashboard listed here is rendered at these tick times, in order, instead of
# once. Omitting `snapshot` advances the animation without comparing a frame.
ANIMATION_FRAME_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_DASHBOARD): cv.string_strict,
        cv.Required(CONF_NOW_MS): cv.int_range(min=0, max=0xFFFFFFFF),
        cv.Optional(CONF_SNAPSHOT): cv.string_strict,
        cv.Optional(CONF_BASE_VISIBLE, default=True): cv.boolean,
        cv.Optional(CONF_STOPWATCH_ELAPSED_MS, default=0): cv.int_range(
            min=0, max=3_600_000
        ),
        cv.Optional(CONF_STOPWATCH_RUNNING, default=False): cv.boolean,
    }
)

CONFIG_SCHEMA = display.FULL_DISPLAY_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(RenderTestDisplay),
        cv.Required(CONF_CONTENT_CONTROLLER): cv.use_id(ContentController),
        cv.Optional(CONF_EQUALIZER): cv.use_id(EqualizerDashboard),
        cv.Required(CONF_PANEL_TEXT): cv.use_id(text.Text),
        cv.Optional(CONF_ANIMATION_FRAMES, default=[]): cv.ensure_list(
            ANIMATION_FRAME_SCHEMA
        ),
        cv.Required(CONF_OUTPUT_DIR): cv.string_strict,
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)
    cg.add(
        var.set_content_controller(
            await cg.get_variable(config[CONF_CONTENT_CONTROLLER])
        )
    )
    if CONF_EQUALIZER in config:
        cg.add(
            var.set_equalizer_dashboard(
                await cg.get_variable(config[CONF_EQUALIZER])
            )
        )
    cg.add(var.set_panel_text(await cg.get_variable(config[CONF_PANEL_TEXT])))
    for frame in config[CONF_ANIMATION_FRAMES]:
        cg.add(
            var.add_animation_frame(
                frame[CONF_DASHBOARD],
                frame[CONF_NOW_MS],
                frame.get(CONF_SNAPSHOT, ""),
                frame[CONF_BASE_VISIBLE],
                frame[CONF_STOPWATCH_ELAPSED_MS],
                frame[CONF_STOPWATCH_RUNNING],
            )
        )
    cg.add(var.set_output_dir(config[CONF_OUTPUT_DIR]))
