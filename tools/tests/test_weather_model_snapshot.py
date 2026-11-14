"""Host regression for the weather view's snapshot boundary."""
import pathlib
import subprocess
import tempfile
import unittest


class WeatherModelSnapshotTest(unittest.TestCase):
    def test_build_view_uses_one_coherent_snapshot(self):
        firmware = pathlib.Path(__file__).resolve().parents[2]
        weather = firmware / 'pixoo_content/include/dashboard/weather'
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            for name, body in {
                'esphome/components/display/display.h': 'namespace esphome::display { class Display {}; }',
                'esphome/components/font/font.h': 'namespace esphome::font { class Font {}; }',
                'esphome/components/time/real_time_clock.h': '''
#pragma once
#include <ctime>
#include <cstddef>
namespace esphome {
struct ESPTime {
  int hour = 0; time_t timestamp = 0;
  static ESPTime from_epoch_utc(time_t) { return {}; }
  bool is_valid() const { return false; }
  void strftime(char *, std::size_t, const char *) const {}
};
namespace time { class RealTimeClock { public: ESPTime now() { return {}; } }; }
}
''',
            }.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#pragma once\n' + body + '\n')
            (root / 'test.cpp').write_text('''
#include <cassert>
#include "esphome/components/pixoo64_content/dashboard/weather/weather_model.h"
struct Fake : pixoo::WeatherSource {
  mutable int reads = 0, legacy = 0;
  bool present = true;
  void RequestRefresh() override {}
  bool HasData() const override { ++legacy; return true; }
  pixoo::WeatherData Data() const override { ++legacy; return {}; }
  pixoo::WeatherSnapshot ReadSnapshot() const override {
    ++reads;
    pixoo::WeatherSnapshot s{};
    s.available = present;
    s.data.has_temperature = true;
    s.data.temperature = 23;
    return s;
  }
};
int main() {
  Fake source;
  esphome::pixoo64::weather::WeatherModel model;
  model.set_source(&source);
  auto v = model.BuildView();
  assert(source.reads == 1 && source.legacy == 0);
  assert(v.has_temperature && v.temperature == 23);
  source.present = false;
  v = model.BuildView();
  assert(source.reads == 2 && source.legacy == 0 && !v.has_temperature);
}
''')
            sources = firmware / 'pixoo_content/src/dashboard/weather'
            subprocess.run(['c++', '-std=c++17', '-pthread', '-I', str(root),
                            '-I', str(firmware), '-I', str(weather),
                            str(root / 'test.cpp'), str(sources / 'weather_data.cpp'),
                            str(sources / 'sky_astronomy.cpp'), '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
