from pathlib import Path
import re

import esphome.codegen as cg
from esphome.components import select
from esphome.components import time as time_
import esphome.config_validation as cv
from esphome.const import CONF_RESTORE_VALUE, CONF_TIME_ID, ENTITY_CATEGORY_CONFIG
from esphome.core.config import include_file

from .. import pixoo64_ns

TimezoneSelect = pixoo64_ns.class_("TimezoneSelect", select.Select, cg.Component)

# The C++ catalog and code generation consume the same ordered X-macro source.
CATALOG_PATH = Path(__file__).resolve().parents[4] / "pixoo_app/src/timezone_catalog.inc"


def parse_catalog(source):
    """Read the deliberately restricted X-macro format; reject other C++ syntax."""
    from aioesphomeapi.posix_tz import parse_posix_tz

    entry_re = re.compile(r'PIXOO_TIMEZONE\("([^"\\]+)", "([^"\\]+)"\)')
    default_re = re.compile(r'PIXOO_DEFAULT_TIMEZONE\("([^"\\]+)"\)')
    # Require complete DST rules and full consumption: the upstream parser also
    # accepts trailing characters and DST names without rules.
    name = r"(?:[A-Za-z]{3,}|<[A-Za-z0-9+-]+>)"
    offset = r"[+-]?[0-9]+(?::[0-9]+(?::[0-9]+)?)?"
    rule = rf"(?:M[0-9]+\.[0-9]+\.[0-9]+|J[0-9]+|[0-9]+)(?:/{offset})?"
    posix_re = re.compile(rf"{name}{offset}(?:{name}(?:{offset})?,{rule},{rule})?")
    entries = []
    labels = set()
    default = None
    for line_number, line in enumerate(source.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        if match := entry_re.fullmatch(line):
            label, posix = match.groups()
            if label in labels:
                raise ValueError(f"Duplicate timezone label: {label}")
            if not posix_re.fullmatch(posix):
                raise ValueError(f"Invalid complete POSIX timezone: {posix}")
            entries.append((label, posix, parse_posix_tz(posix)))
            labels.add(label)
        elif match := default_re.fullmatch(line):
            if default is not None:
                raise ValueError("Duplicate default timezone")
            default = match[1]
        else:
            raise ValueError(f"Unexpected timezone catalog syntax on line {line_number}")
    if len(entries) != 44:
        raise ValueError("Timezone catalog must contain 44 persisted indexes")
    if default not in labels:
        raise ValueError("Default timezone must resolve to a catalog entry")
    return entries, default


def timezone_array_cpp(entries, symbol):
    """Emit flash-resident ParsedTimezone aggregates in persisted catalog order."""
    def rule_cpp(rule):
        return (
            f"{{{rule.time_seconds}, {rule.day}, "
            f"esphome::time::DSTRuleType::{rule.type.name}, "
            f"{rule.month}, {rule.week}, {rule.day_of_week}}}"
        )

    rows = [
        f"  {{{tz.std_offset_seconds}, {tz.dst_offset_seconds}, "
        f"{rule_cpp(tz.dst_start)}, {rule_cpp(tz.dst_end)}}},"
        for _, _, tz in entries
    ]
    return (
        f"static const esphome::time::ParsedTimezone {symbol}[] = {{\n"
        + "\n".join(rows)
        + f"\n}};\nstatic_assert(sizeof({symbol}) / sizeof({symbol}[0]) == 44, "
        '"Timezone indexes are persisted");'
    )
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
    entries, _ = parse_catalog(CATALOG_PATH.read_text(encoding="utf-8"))
    # X-macro inputs are copied, not included at main.cpp scope.
    include_file(CATALOG_PATH, Path(CATALOG_PATH.name))
    cg.add_define("USE_TIME_TIMEZONE")
    var = await select.new_select(config, options=[])
    symbol = f"{var.base}_timezones"
    cg.add_global(cg.RawStatement(timezone_array_cpp(entries, symbol)))
    cg.add(var.set_timezones(cg.RawExpression(symbol), len(entries)))
    await cg.register_component(var, config)
    cg.add(var.set_time(await cg.get_variable(config[CONF_TIME_ID])))
    cg.add(var.set_restore_value(config[CONF_RESTORE_VALUE]))
