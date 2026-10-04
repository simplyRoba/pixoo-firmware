"""Canonical timezone codegen and real ESPHome civil-time conversion tests."""
import ast
import asyncio
import calendar
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import unittest
from unittest.mock import AsyncMock, Mock

import esphome

ROOT = Path(__file__).resolve().parents[2]
SELECT = ROOT / "esphome/components/pixoo64/select"
CATALOG = ROOT / "pixoo_app/src/timezone_catalog.inc"
UPSTREAM = Path(esphome.__file__).parent

# Load the production pure helpers without loading the whole device schema.
_tree = ast.parse((SELECT / "__init__.py").read_text())
_helpers = ast.Module(body=[node for node in _tree.body
                           if isinstance(node, ast.FunctionDef)
                           and node.name in ("parse_catalog", "timezone_array_cpp")],
                      type_ignores=[])
_namespace = {"re": re}
exec(compile(_helpers, str(SELECT / "__init__.py"), "exec"), _namespace)
parse_catalog = _namespace["parse_catalog"]
timezone_array_cpp = _namespace["timezone_array_cpp"]


class TimezoneCodegenTest(unittest.TestCase):
    def setUp(self):
        self.source = CATALOG.read_text()
        self.entries, self.default = parse_catalog(self.source)

    def test_catalog_persisted_identity(self):
        identity = json.dumps(([(label, posix) for label, posix, _ in self.entries],
                               self.default)).encode()
        self.assertEqual(hashlib.sha256(identity).hexdigest(),
                         "7416bbd464d07a66661aae2d7d202947657000521b4aae794388571f00673a37")
        self.assertEqual(len(self.entries), 44)
        self.assertEqual(self.entries[8][0], self.default)

    def test_strict_catalog_validation(self):
        for source in (
            self.source + "unexpected syntax\n",
            self.source + 'PIXOO_DEFAULT_TIMEZONE("UTC")\n',
            self.source.replace('"SST11"', '"SST11junk"'),
            self.source.replace('"SST11"', '"SST11DST"'),
            self.source.replace('"SST11"', '"SST11DST,M13.1.0,M4.1.0"'),
            self.source.replace('"Honolulu (UTC-10)"', '"Midway (UTC-11)"'),
            self.source.replace('PIXOO_DEFAULT_TIMEZONE("New York (UTC-5)")',
                                'PIXOO_DEFAULT_TIMEZONE("Unknown")'),
            self.source.replace('PIXOO_TIMEZONE("Midway (UTC-11)", "SST11")\n', ''),
        ):
            with self.subTest(source=source), self.assertRaises(ValueError):
                parse_catalog(source)

    def test_offsets_and_transition_rules(self):
        zones = {label.split(" (")[0]: tz for label, _, tz in self.entries}
        for name, offset in (("New York", 18000), ("Kathmandu", -20700),
                             ("Kolkata", -19800), ("Adelaide", -34200),
                             ("Santiago", 14400), ("Cairo", -7200)):
            self.assertEqual(zones[name].std_offset_seconds, offset)
        for name, start, end in (
            ("New York", (3, 2, 0, 7200), (11, 1, 0, 7200)),
            ("Santiago", (9, 1, 6, 86400), (4, 1, 6, 86400)),
            ("Cairo", (4, 5, 5, 0), (10, 5, 4, 86400)),
            ("Adelaide", (10, 1, 0, 7200), (4, 1, 0, 10800)),
            ("Auckland", (9, 5, 0, 7200), (4, 1, 0, 10800)),
        ):
            tz = zones[name]
            self.assertTrue(tz.has_dst)
            self.assertEqual(tz.dst_offset_seconds, tz.std_offset_seconds - 3600)
            for rule, expected in ((tz.dst_start, start), (tz.dst_end, end)):
                self.assertEqual(rule.type.name, "MONTH_WEEK_DAY")
                self.assertEqual((rule.month, rule.week, rule.day_of_week,
                                  rule.time_seconds), expected)
        self.assertFalse(zones["Kathmandu"].has_dst)
        cpp = timezone_array_cpp(self.entries, "zones")
        self.assertIn("static const esphome::time::ParsedTimezone zones[]", cpp)
        self.assertIn("static_assert(sizeof(zones) / sizeof(zones[0]) == 44", cpp)

    def test_codegen_injects_flash_table_and_enables_timezone_source(self):
        node = next(node for node in _tree.body
                    if isinstance(node, ast.AsyncFunctionDef) and node.name == "to_code")
        var = Mock()
        var.base = "timezone_select"
        cg = Mock()
        cg.RawStatement.side_effect = lambda value: value
        cg.RawExpression.side_effect = lambda value: value
        cg.register_component = AsyncMock()
        cg.get_variable = AsyncMock(return_value="rtc")
        select = Mock()
        select.new_select = AsyncMock(return_value=var)
        copy_input = Mock()
        namespace = dict(_namespace, cg=cg, select=select, include_file=copy_input,
                         CATALOG_PATH=CATALOG, Path=Path,
                         CONF_TIME_ID="time_id", CONF_RESTORE_VALUE="restore_value")
        exec(compile(ast.Module(body=[node], type_ignores=[]), "timezone_to_code", "exec"),
             namespace)
        config = {"time_id": "clock", "restore_value": True}
        asyncio.run(namespace["to_code"](config))
        copy_input.assert_called_once_with(CATALOG, Path("timezone_catalog.inc"))
        cg.add_define.assert_called_once_with("USE_TIME_TIMEZONE")
        cg.add_global.assert_called_once_with(
            timezone_array_cpp(self.entries, "timezone_select_timezones"))
        var.set_timezones.assert_called_once_with("timezone_select_timezones", 44)
        var.set_time.assert_called_once_with("rtc")
        select.new_select.assert_awaited_once_with(config, options=[])

    @unittest.skipUnless(shutil.which("c++") and hasattr(time, "tzset"),
                         "requires native C++ compiler and POSIX libc")
    def test_runtime_selector_against_upstream_time_conversion(self):
        # libc provides an independent oracle for all catalog rules, including
        # both sides of every transition, without requiring live hardware.
        checks = []
        old_tz = os.environ.get("TZ")
        try:
            for index, (_, posix, tz) in enumerate(self.entries):
                os.environ["TZ"] = posix
                time.tzset()
                epochs = [int(datetime(2026, month, 15, 12, tzinfo=timezone.utc).timestamp())
                          for month in range(1, 13)]
                if tz.has_dst:
                    for rule, offset in ((tz.dst_start, tz.std_offset_seconds),
                                         (tz.dst_end, tz.dst_offset_seconds)):
                        days = [day for day in range(1, calendar.monthrange(2026, rule.month)[1] + 1)
                                if (datetime(2026, rule.month, day).weekday() + 1) % 7
                                == rule.day_of_week]
                        day = days[-1] if rule.week == 5 else days[rule.week - 1]
                        epoch = int(datetime(2026, rule.month, day,
                                             tzinfo=timezone.utc).timestamp())
                        epoch += rule.time_seconds + offset
                        epochs.extend((epoch - 1, epoch, epoch + 1))
                for epoch in epochs:
                    local = time.localtime(epoch)
                    checks.append(f"check({index}, {epoch}, {local.tm_year}, {local.tm_mon}, "
                                  f"{local.tm_mday}, {local.tm_hour}, {local.tm_min}, "
                                  f"{local.tm_sec}, {local.tm_isdst});")
        finally:
            if old_tz is None:
                os.environ.pop("TZ", None)
            else:
                os.environ["TZ"] = old_tz
            time.tzset()

        stubs = {
            "esphome/core/defines.h": "#pragma once\n",
            "esphome/core/component.h": """
#pragma once
namespace esphome {
namespace setup_priority { constexpr float DATA = 600; }
class Component {
 bool failed_ = false;
 public:
 virtual void setup() {} virtual void dump_config() {}
 virtual float get_setup_priority() const { return 0; }
 void mark_failed() { failed_ = true; } bool is_failed() const { return failed_; }
};
}
""",
            "esphome/core/preferences.h": """
#pragma once
#include <cstddef>
namespace esphome {
inline bool stored_valid = false;
inline size_t stored_index = 0;
struct ESPPreferenceObject {
 bool load(size_t *index) { if (!stored_valid) return false; *index = stored_index; return true; }
 void save(const size_t *index) { stored_valid = true; stored_index = *index; }
};
}
""",
            "esphome/core/helpers.h": "#pragma once\n",
            "esphome/core/log.h": "#pragma once\n#define LOG_SELECT(...)\n#define ESP_LOGCONFIG(...)\n#define ESP_LOGE(...)\n",
            "esphome/components/time/real_time_clock.h": "#pragma once\nnamespace esphome::time { class RealTimeClock {}; }\n",
            "esphome/components/select/select.h": """
#pragma once
#include <vector>
#include "esphome/core/preferences.h"
namespace esphome {
template<typename T> struct FixedVector : std::vector<T> { void init(size_t n) { this->reserve(n); } };
namespace select {
struct Traits {
 FixedVector<const char *> options;
 void set_options(const FixedVector<const char *> &o) { options = o; }
};
class Select {
 public:
 Traits traits; size_t published = 999;
 void publish_state(size_t index) { published = index; }
 template<typename T> ESPPreferenceObject make_entity_preference() { return {}; }
 protected: virtual void control(size_t index) = 0;
};
}
}
""",
        }
        harness = r'''
#include <cassert>
#include <cstring>
#include "timezone_select.h"
#include "timezone_catalog.h"
class TestSelect : public esphome::pixoo64::TimezoneSelect {
 public: using TimezoneSelect::control;
};
''' + timezone_array_cpp(self.entries, "zones") + r'''
TestSelect selector;
void check(size_t index, time_t epoch, int year, int month, int day,
           int hour, int minute, int second, int dst) {
 selector.control(index);
 assert(selector.published == index);
 tm local{};
 assert(esphome::time::epoch_to_local_tm(epoch, esphome::time::get_global_tz(), &local));
 assert(local.tm_year + 1900 == year && local.tm_mon + 1 == month && local.tm_mday == day);
 assert(local.tm_hour == hour && local.tm_min == minute && local.tm_sec == second);
 assert(local.tm_isdst == dst);
}
int main() {
 esphome::time::RealTimeClock rtc;
 selector.set_time(&rtc); selector.set_timezones(zones, 44); selector.setup();
 assert(!selector.is_failed() && selector.published == 8);
 assert(selector.traits.options.size() == pixoo::TimezoneCount());
 for (size_t i = 0; i < 44; ++i)
   assert(std::strcmp(selector.traits.options[i], pixoo::TimezoneLabel(i)) == 0);
''' + "\n".join(checks) + r'''
 auto previous = selector.published;
 selector.control(44); assert(selector.published == previous);
 TestSelect restored;
 restored.set_time(&rtc); restored.set_timezones(zones, 44); restored.set_restore_value(true);
 esphome::stored_valid = true; esphome::stored_index = 29;
 restored.setup(); assert(restored.published == 29);
 assert(esphome::time::get_global_tz().std_offset_seconds == -20700);
 restored.control(37); assert(esphome::stored_index == 37);
 esphome::stored_index = 100;
 restored.setup(); assert(restored.published == 8);
 TestSelect invalid;
 invalid.set_time(&rtc); invalid.set_timezones(zones, 43); invalid.setup();
 assert(invalid.is_failed()); invalid.control(0); assert(invalid.published == 999);
 TestSelect missing_rtc;
 missing_rtc.set_timezones(zones, 44); missing_rtc.setup(); assert(missing_rtc.is_failed());
}
'''
        with tempfile.TemporaryDirectory(prefix="pixoo-timezone-") as tmp:
            tmp = Path(tmp)
            for path, source in stubs.items():
                dest = tmp / path
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_text(source)
            (tmp / "test.cpp").write_text(harness)
            command = ["c++", "-std=c++17", "-DUSE_TIME_TIMEZONE",
                       f"-I{tmp}", f"-I{UPSTREAM.parent}", f"-I{SELECT}",
                       f"-I{ROOT / 'pixoo_app/include'}", str(tmp / "test.cpp"),
                       str(SELECT / "timezone_select.cpp"),
                       str(ROOT / "pixoo_app/src/timezone_catalog.cpp"),
                       str(UPSTREAM / "components/time/posix_tz.cpp"),
                       "-o", str(tmp / "test")]
            subprocess.run(command, check=True, capture_output=True, text=True)
            subprocess.run([str(tmp / "test")], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
