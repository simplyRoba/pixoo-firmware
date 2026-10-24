#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "firmware_app.h"
#include "app_state.h"
#include "frame_metrics.h"
#include "light_state_publication_guard.h"
#include "timezone_catalog.h"
#include "pixoo_output.h"

using namespace pixoo;

void RunDashboardRestoreTests();

void setUp() {}
void tearDown() {}

namespace {

const uint8_t kBootToken[] = {0xb0};
const uint8_t kBaseToken[] = {0xba};
const uint8_t kNotificationToken[] = {0xbe};
const uint8_t kReactionToken[] = {0xbc};
const uint8_t kFirmwareUpdateToken[] = {0xbf};

const char *TokenName(FrameView frame) {
  if (frame.data == kBootToken && frame.size == sizeof(kBootToken))
    return "boot";
  if (frame.data == kBaseToken && frame.size == sizeof(kBaseToken))
    return "base";
  if (frame.data == kNotificationToken && frame.size == sizeof(kNotificationToken))
    return "notification";
  if (frame.data == kReactionToken && frame.size == sizeof(kReactionToken))
    return "reaction";
  if (frame.data == kFirmwareUpdateToken &&
      frame.size == sizeof(kFirmwareUpdateToken))
    return "firmware-update";
  return "unexpected";
}

struct RecordingPanel final : PanelPort {
  explicit RecordingPanel(std::vector<std::string> *events) : events(events) {}

  void SetPower(bool on) override {
    events->push_back(on ? "power:on" : "power:off");
    ++power_calls;
    last_power_on_delay_ms = on && !power_on ? power_on_delay_ms : 0;
    power_on = on;
  }
  uint32_t LastPowerOnDelayMs() const override {
    return last_power_on_delay_ms;
  }
  bool Initialize() override {
    events->push_back("initialize");
    ++initialize_calls;
    if (initialize_result_index < initialize_results.size())
      return initialize_results[initialize_result_index++];
    return true;
  }
  void SetBrightness(float brightness) override {
    const int percent = static_cast<int>(std::lround(brightness * 100.0f));
    events->push_back("brightness:" + std::to_string(percent));
  }
  bool Present(FrameView frame, bool force) override {
    events->push_back(std::string(force ? "present:force:" : "present:normal:") +
                      TokenName(frame));
    received_tokens.push_back(TokenName(frame));
    if (present_result_index < present_results.size())
      return present_results[present_result_index++];
    return true;
  }

  std::vector<std::string> *events;
  std::vector<std::string> received_tokens;
  std::vector<bool> initialize_results;
  size_t initialize_result_index{0};
  std::vector<bool> present_results;
  size_t present_result_index{0};
  int power_calls{0};
  int initialize_calls{0};
  uint32_t power_on_delay_ms{0};
  uint32_t last_power_on_delay_ms{0};
  bool power_on{false};
};

struct RecordingRenderer final : RenderPort {
  explicit RecordingRenderer(std::vector<std::string> *events) : events(events) {
    catalog = {{"text", false}, {"weather", true}, {"equalizer", true}};
  }

  bool ResolveDashboard(const std::string &requested,
                        DashboardSelection *selection) override {
    events->push_back("resolve:" + requested);
    for (const auto &candidate : catalog) {
      if (candidate.id == requested) {
        *selection = candidate;
        return true;
      }
    }
    if (!fallback_id.empty()) {
      for (const auto &candidate : catalog) {
        if (candidate.id == fallback_id) {
          *selection = candidate;
          return true;
        }
      }
    }
    return false;
  }
  FrameView RenderBootAnimation(uint32_t elapsed_ms) override {
    events->push_back("boot:" + std::to_string(elapsed_ms));
    return boot_frame;
  }
  FrameView RenderFirmwareUpdate() override {
    events->push_back("firmware-update");
    return firmware_update_frame;
  }
  uint32_t NotificationMinVisibleMs(
      const Notification &notification) override {
    events->push_back("min-visible:" + std::string(notification.text.c_str()));
    return min_visible_ms;
  }
  void HideBaseContent(uint32_t now_ms) override {
    ++hide_base_calls;
    hide_base_times.push_back(now_ms);
  }
  void ReleaseOverlayResources() override { release_overlay_calls++; }
  bool RenderContent(uint32_t now_ms, const std::string &dashboard_id,
                     const TimingSnapshot &timing, const Overlay *overlay,
                     uint32_t overlay_visible_elapsed_ms, bool base_visible,
                     bool base_frozen, bool render_base, bool render_overlay,
                     FrameView *frame) override {
    last_render_now_ms = now_ms;
    last_timing = timing;
    render_base_calls.push_back(render_base);
    render_overlay_calls.push_back(render_overlay);
    base_frozen_calls.push_back(base_frozen);
    base_visible_calls.push_back(base_visible);
    std::string event = "content:" + dashboard_id;
    if (overlay == nullptr || !render_overlay) {
      event += render_base ? ":base" : ":cached-base";
      *frame = base_frame;
    } else if (overlay->tag == OverlayTag::kNotification) {
      event += ":" + std::string(overlay->notification.text.c_str()) + ":" +
               std::to_string(overlay_visible_elapsed_ms) +
               (base_visible ? ":live" : ":black");
      *frame = notification_frame;
    } else {
      event += ":reaction:";
      event += ReactionName(overlay->reaction);
      event += ":" + std::to_string(overlay_visible_elapsed_ms) +
               (base_visible ? ":live" : ":black");
      *frame = reaction_frame;
    }
    events->push_back(std::move(event));
    if (render_result_index < render_results.size() &&
        !render_results[render_result_index++])
      return false;
    return true;
  }

  std::vector<std::string> *events;
  std::vector<DashboardSelection> catalog;
  std::string fallback_id{"text"};
  FrameView boot_frame{kBootToken, sizeof(kBootToken)};
  FrameView firmware_update_frame{kFirmwareUpdateToken,
                                  sizeof(kFirmwareUpdateToken)};
  FrameView base_frame{kBaseToken, sizeof(kBaseToken)};
  FrameView notification_frame{kNotificationToken, sizeof(kNotificationToken)};
  FrameView reaction_frame{kReactionToken, sizeof(kReactionToken)};
  uint32_t min_visible_ms{0};
  uint32_t last_render_now_ms{0};
  TimingSnapshot last_timing{};
  int hide_base_calls{0};
  std::vector<uint32_t> hide_base_times;
  int release_overlay_calls{0};
  std::vector<bool> render_results;
  size_t render_result_index{0};
  std::vector<bool> render_base_calls;
  std::vector<bool> render_overlay_calls;
  std::vector<bool> base_frozen_calls;
  std::vector<bool> base_visible_calls;
};

struct RecordingMicrophone final : MicrophonePort {
  explicit RecordingMicrophone(std::vector<std::string> *events)
      : events(events) {}
  void SetEnabled(bool enabled) override {
    events->push_back(enabled ? "microphone:on" : "microphone:off");
    calls.push_back(enabled);
  }
  std::vector<std::string> *events;
  std::vector<bool> calls;
};

const char *SoundName(Sound sound) {
  switch (sound) {
    case Sound::kBoot:
      return "boot";
    case Sound::kChirp:
      return "chirp";
    case Sound::kSuccess:
      return "success";
    default:
      return "other";
  }
}

struct RecordingSound final : SoundPlayer {
  explicit RecordingSound(std::vector<std::string> *events) : events(events) {}
  void Play(Sound sound) override {
    events->push_back(std::string("sound:") + SoundName(sound));
    played.push_back(sound);
  }
  void Stop() override {
    events->push_back("sound:stop");
    ++stop_calls;
  }

  std::vector<std::string> *events;
  std::vector<Sound> played;
  int stop_calls{0};
};

struct RecordingLightSink final : LightStateSink {
  void Publish(LightState state, bool persistent) override {
    published.push_back(state);
    persistent_publications.push_back(persistent);
  }
  std::vector<LightState> published;
  std::vector<bool> persistent_publications;
};

struct RecordingSolarBrightnessSink final : SolarBrightnessStateSink {
  void PublishSolarBrightnessEnabled(bool enabled) override {
    published.push_back(enabled);
  }
  std::vector<bool> published;
};

struct RecordingSystem final : SystemPort {
  explicit RecordingSystem(std::vector<std::string> *events) : events(events) {}
  void Reboot() override {
    events->push_back("system:reboot");
    ++reboot_calls;
  }
  void FactoryReset() override {
    events->push_back("system:factory-reset");
    ++factory_reset_calls;
  }
  std::vector<std::string> *events;
  int reboot_calls{0};
  int factory_reset_calls{0};
};

struct RecordingFrameMetrics final : FrameMetricsPort {
  void BeginRegularFrame() override {
    ++begin_calls;
    active = true;
  }
  void EndRegularFrame() override {
    ++end_calls;
    active = false;
  }
  int begin_calls{0};
  int end_calls{0};
  bool active{false};
};

void AssertEvent(const std::vector<std::string> &events, size_t index,
                 const char *expected) {
  TEST_ASSERT_TRUE(index < events.size());
  TEST_ASSERT_EQUAL_STRING(expected, events[index].c_str());
}

NotificationRequest Request(const char *text, uint32_t duration_ms) {
  return NotificationRequest{Notification{text, Severity::kInfo}, duration_ms,
                             false, Sound::kChirp};
}

void StartRunning(FirmwareApp *app, uint32_t now_ms = 0) {
  app->Start(now_ms, LightState{true, 0.5f}, "text");
  app->Tick(now_ms);
  app->Tick(now_ms);
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kRunning),
                    static_cast<int>(app->phase()));
}

int CountEvent(const std::vector<std::string> &events, const char *expected) {
  int count = 0;
  for (const std::string &event : events) {
    if (event == expected)
      ++count;
  }
  return count;
}

void AssertLight(const LightState &actual, bool on, float brightness) {
  TEST_ASSERT_EQUAL(on, actual.on);
  TEST_ASSERT_EQUAL_FLOAT(brightness, actual.brightness);
}

}  // namespace

static void test_notification_duration_seconds_convert_to_milliseconds() {
  TEST_ASSERT_EQUAL_UINT32(4000, NotificationDurationMsFromSeconds(0));
  TEST_ASSERT_EQUAL_UINT32(4000, NotificationDurationMsFromSeconds(-1));
  TEST_ASSERT_EQUAL_UINT32(1000, NotificationDurationMsFromSeconds(1));
  TEST_ASSERT_EQUAL_UINT32(300000, NotificationDurationMsFromSeconds(300));
  TEST_ASSERT_EQUAL_UINT32(
      4294967000U, NotificationDurationMsFromSeconds(4294967));
  TEST_ASSERT_EQUAL_UINT32(
      4294967000U, NotificationDurationMsFromSeconds(4294968));
  TEST_ASSERT_EQUAL_UINT32(
      4294967000U,
      NotificationDurationMsFromSeconds(std::numeric_limits<int32_t>::max()));
}

static void test_timer_seconds_reject_invalid_duration() {
  TEST_ASSERT_EQUAL_UINT32(0, TimerDurationMsFromSeconds(-1));
  TEST_ASSERT_EQUAL_UINT32(0, TimerDurationMsFromSeconds(0));
  TEST_ASSERT_EQUAL_UINT32(1000, TimerDurationMsFromSeconds(1));
  TEST_ASSERT_EQUAL_UINT32(5999000, TimerDurationMsFromSeconds(5999));
  TEST_ASSERT_EQUAL_UINT32(0, TimerDurationMsFromSeconds(6000));
  TEST_ASSERT_EQUAL_UINT32(0, TimerDurationMsFromSeconds(INT32_MAX));
  TEST_ASSERT_EQUAL_STRING("none", TimingKindName(TimingKind::kNone));
  TEST_ASSERT_EQUAL_STRING("stopwatch", TimingKindName(TimingKind::kStopwatch));
  TEST_ASSERT_EQUAL_STRING("timer", TimingKindName(TimingKind::kTimer));
  TEST_ASSERT_EQUAL_STRING("idle", TimingPhaseName(TimingPhase::kIdle));
  TEST_ASSERT_EQUAL_STRING("running", TimingPhaseName(TimingPhase::kRunning));
  TEST_ASSERT_EQUAL_STRING("paused", TimingPhaseName(TimingPhase::kPaused));
  TEST_ASSERT_EQUAL_STRING("finished", TimingPhaseName(TimingPhase::kFinished));
}

static void test_timezone_catalog_maps_labels_to_posix_with_valid_default() {
  // Every label resolves to a unique index whose POSIX string round-trips.
  const size_t count = TimezoneCount();
  TEST_ASSERT_GREATER_THAN(0u, count);
  for (size_t i = 0; i < count; i++) {
    const char *label = TimezoneLabel(i);
    const char *posix = TimezonePosix(i);
    TEST_ASSERT_NOT_NULL(label);
    TEST_ASSERT_NOT_NULL(posix);
    TEST_ASSERT_TRUE(posix[0] != '\0');
    size_t resolved = count;  // sentinel
    TEST_ASSERT_TRUE(TimezoneIndexForLabel(label, &resolved));
    TEST_ASSERT_EQUAL(i, resolved);
  }

  // Out-of-range access is null; unknown/null labels do not resolve and leave
  // the caller's index untouched.
  TEST_ASSERT_NULL(TimezoneLabel(count));
  TEST_ASSERT_NULL(TimezonePosix(count));
  size_t index = 12345;
  TEST_ASSERT_FALSE(TimezoneIndexForLabel("Nowhere (UTC+99)", &index));
  TEST_ASSERT_EQUAL(12345u, index);
  TEST_ASSERT_FALSE(TimezoneIndexForLabel(nullptr, &index));
  TEST_ASSERT_EQUAL(12345u, index);

  // The default index is in range and points at the documented default.
  const size_t def = DefaultTimezoneIndex();
  TEST_ASSERT_LESS_THAN(count, def);
  TEST_ASSERT_EQUAL_STRING("New York (UTC-5)", TimezoneLabel(def));
  TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", TimezonePosix(def));
}

static void test_severity_parse_roundtrips_and_falls_back() {
  TEST_ASSERT_EQUAL(static_cast<int>(Severity::kInfo),
                    static_cast<int>(ParseSeverity("info")));
  TEST_ASSERT_EQUAL(static_cast<int>(Severity::kSuccess),
                    static_cast<int>(ParseSeverity("success")));
  TEST_ASSERT_EQUAL(static_cast<int>(Severity::kWarning),
                    static_cast<int>(ParseSeverity("warning")));
  TEST_ASSERT_EQUAL(static_cast<int>(Severity::kError),
                    static_cast<int>(ParseSeverity("error")));
  TEST_ASSERT_EQUAL(static_cast<int>(Severity::kInfo),
                    static_cast<int>(ParseSeverity("bogus")));
  TEST_ASSERT_EQUAL_STRING("warning", SeverityName(Severity::kWarning));
}

static void test_reaction_parse_roundtrips_and_has_designed_durations() {
  const Reaction reactions[] = {
      Reaction::kLaughing,   Reaction::kLove,       Reaction::kCrying,
      Reaction::kAngry,      Reaction::kPoop,       Reaction::kApprove,
      Reaction::kDisapprove, Reaction::kCelebrate,  Reaction::kThinking,
      Reaction::kSurprised,  Reaction::kFire,       Reaction::kEyes,
  };
  for (Reaction reaction : reactions) {
    TEST_ASSERT_EQUAL(static_cast<int>(reaction),
                      static_cast<int>(ParseReaction(ReactionName(reaction))));
  }
  TEST_ASSERT_EQUAL(static_cast<int>(Reaction::kLaughing),
                    static_cast<int>(ParseReaction("bogus")));
  TEST_ASSERT_EQUAL_UINT32(1500, ReactionVisibleDurationMs(Reaction::kApprove));
  TEST_ASSERT_EQUAL_UINT32(1800, ReactionVisibleDurationMs(Reaction::kLaughing));
  TEST_ASSERT_EQUAL_UINT32(2000, ReactionVisibleDurationMs(Reaction::kLove));
  TEST_ASSERT_EQUAL_UINT32(2200, ReactionVisibleDurationMs(Reaction::kCrying));
}

static void test_frame_handoff_uses_renderer_views_exactly() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 2});

  app.Start(0, LightState{true, 0.5f}, "text");
  app.Tick(0);  // initialize
  app.Tick(1);  // boot frame
  app.Tick(2);  // base frame
  app.Notify(Request("Alert", 10), 3);
  app.Tick(3);  // notification frame

  TEST_ASSERT_EQUAL(3, panel.received_tokens.size());
  TEST_ASSERT_EQUAL_STRING("boot", panel.received_tokens[0].c_str());
  TEST_ASSERT_EQUAL_STRING("base", panel.received_tokens[1].c_str());
  TEST_ASSERT_EQUAL_STRING("notification", panel.received_tokens[2].c_str());
  AssertEvent(events, 5, "present:normal:boot");
  AssertEvent(events, 7, "present:normal:base");
  AssertEvent(events, 9, "present:force:notification");
}

static void test_firmware_update_presents_static_frame_synchronously() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  RecordingMicrophone microphone(&events);
  FirmwareApp app(panel, renderer, &sound, &microphone, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  app.SelectDashboard("weather");

  NotificationRequest request = Request("Update", 100);
  request.has_sound = true;
  app.Notify(request, 10);
  app.Tick(10);
  events.clear();

  TEST_ASSERT_TRUE(app.BeginFirmwareUpdate(10));

  TEST_ASSERT_EQUAL(2, renderer.hide_base_calls);
  TEST_ASSERT_EQUAL_UINT32(10, renderer.hide_base_times.back());
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kRunning),
                    static_cast<int>(app.phase()));
  TEST_ASSERT_TRUE(app.notification_visible());
  AssertEvent(events, 0, "firmware-update");
  AssertEvent(events, 1, "present:force:firmware-update");

  // If OTA aborts, the next service tick redraws the unchanged application
  // state without waiting for the selected dashboard's cadence.
  events.clear();
  app.Tick(11);
  AssertEvent(events, 0, "content:weather:Update:1:live");
  AssertEvent(events, 1, "present:normal:notification");
  TEST_ASSERT_TRUE(renderer.render_base_calls.back());
  TEST_ASSERT_TRUE(renderer.render_overlay_calls.back());
}

static void test_firmware_update_skips_unready_or_failed_presentation() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{100, 0, 0});

  app.Start(0, LightState{false, 0.5f}, "text");
  events.clear();
  TEST_ASSERT_FALSE(app.BeginFirmwareUpdate(0));
  TEST_ASSERT_TRUE(events.empty());

  app.SetUserLight(LightState{true, 0.5f}, 1);
  events.clear();
  TEST_ASSERT_FALSE(app.BeginFirmwareUpdate(1));
  TEST_ASSERT_TRUE(events.empty());

  app.Tick(101);
  app.Tick(101);
  events.clear();
  renderer.firmware_update_frame = {};
  TEST_ASSERT_FALSE(app.BeginFirmwareUpdate(101));
  TEST_ASSERT_EQUAL(1, events.size());
  AssertEvent(events, 0, "firmware-update");

  app.Notify(Request("Retry", 100), 102);
  app.Tick(102);
  TEST_ASSERT_TRUE(app.notification_visible());

  renderer.firmware_update_frame = {kFirmwareUpdateToken,
                                    sizeof(kFirmwareUpdateToken)};
  panel.present_results = {false};
  events.clear();
  TEST_ASSERT_FALSE(app.BeginFirmwareUpdate(102));
  AssertEvent(events, 0, "firmware-update");
  AssertEvent(events, 1, "present:force:firmware-update");
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kRunning),
                    static_cast<int>(app.phase()));

  events.clear();
  app.Tick(103);
  AssertEvent(events, 0, "content:text:Retry:1:live");
  AssertEvent(events, 1, "present:normal:notification");
  TEST_ASSERT_TRUE(renderer.render_base_calls.back());
  TEST_ASSERT_TRUE(renderer.render_overlay_calls.back());
}

static void test_frame_metrics_window_uses_elapsed_wall_time() {
  FrameMetricsWindow window;
  FrameMetricsSnapshot snapshot;
  window.Record(1000);
  TEST_ASSERT_FALSE(window.Close(100, &snapshot));

  window.Reset(1000);
  for (int i = 0; i < 150; ++i)
    window.Record(2000);
  TEST_ASSERT_TRUE(window.IsDue(7000, 6000));
  TEST_ASSERT_TRUE(window.Close(7000, &snapshot));
  TEST_ASSERT_EQUAL_UINT32(150, snapshot.frames);
  TEST_ASSERT_EQUAL_UINT32(6000, snapshot.elapsed_ms);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, snapshot.average_ms);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, snapshot.maximum_ms);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, snapshot.frames_per_second);

  TEST_ASSERT_FALSE(window.Close(7000, &snapshot));
  TEST_ASSERT_TRUE(window.Close(13000, &snapshot));
  TEST_ASSERT_EQUAL_UINT32(0, snapshot.frames);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, snapshot.frames_per_second);

  window.Reset(UINT32_MAX - 20);
  window.Record(1000);
  TEST_ASSERT_TRUE(window.Close(29, &snapshot));
  TEST_ASSERT_EQUAL_UINT32(50, snapshot.elapsed_ms);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, snapshot.frames_per_second);
}

static void test_regular_frame_metrics_cover_render_through_present_only() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  renderer.catalog[0].frame_interval_ms = 1;
  RecordingFrameMetrics metrics;
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 10}, nullptr, &metrics);

  app.Start(0, LightState{true, 0.5f}, "text");
  app.Tick(0);   // initialize
  app.Tick(1);   // boot animation
  TEST_ASSERT_EQUAL(0, metrics.begin_calls);
  TEST_ASSERT_EQUAL(0, metrics.end_calls);

  app.Tick(10);  // base content
  TEST_ASSERT_EQUAL(1, metrics.begin_calls);
  TEST_ASSERT_EQUAL(1, metrics.end_calls);
  TEST_ASSERT_FALSE(metrics.active);

  renderer.render_results = {false};
  app.Tick(11);  // failed regular render
  renderer.render_results.clear();
  renderer.render_result_index = 0;
  renderer.base_frame = {};
  app.Tick(12);  // invalid regular frame
  renderer.base_frame = {kBaseToken, sizeof(kBaseToken)};
  TEST_ASSERT_EQUAL(3, metrics.begin_calls);
  TEST_ASSERT_EQUAL(3, metrics.end_calls);

  panel.present_results = {false};
  app.Notify(Request("Alert", 10), 13);
  app.Tick(13);  // notification content, failed present
  TEST_ASSERT_EQUAL(4, metrics.begin_calls);
  TEST_ASSERT_EQUAL(4, metrics.end_calls);

  app.BeginFirmwareUpdate(13);
  TEST_ASSERT_EQUAL(4, metrics.begin_calls);
  TEST_ASSERT_EQUAL(4, metrics.end_calls);

  app.SetUserLight(LightState{false, 0.5f}, 14);
  app.Tick(14);
  TEST_ASSERT_EQUAL(4, metrics.begin_calls);
  TEST_ASSERT_EQUAL(4, metrics.end_calls);
}

static void test_invalid_or_failed_frame_is_not_presented() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  renderer.base_frame = {};
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  TEST_ASSERT_EQUAL(0, panel.received_tokens.size());
  renderer.base_frame = {kBaseToken, sizeof(kBaseToken)};
  renderer.render_results = {false};
  app.Tick(1);
  TEST_ASSERT_EQUAL(0, panel.received_tokens.size());
}

static void test_dashboard_metadata_is_trusted_not_caller_supplied() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingMicrophone microphone(&events);
  FirmwareApp app(panel, renderer, nullptr, &microphone, nullptr,
                  FirmwareAppConfig{0, 0, 0});

  // "weather" is not an equalizer name, but the trusted catalog requires mic.
  StartRunning(&app);
  app.SelectDashboard("weather");
  TEST_ASSERT_TRUE(app.selected_dashboard().id == "weather");
  TEST_ASSERT_TRUE(app.selected_dashboard().requires_microphone);
  TEST_ASSERT_EQUAL(1, microphone.calls.size());
  TEST_ASSERT_TRUE(microphone.calls[0]);

  // Public APIs accept only IDs: a caller has no metadata path to forge.
  app.SelectDashboard("missing");
  TEST_ASSERT_TRUE(app.selected_dashboard().id == "text");
  TEST_ASSERT_FALSE(app.selected_dashboard().requires_microphone);
  TEST_ASSERT_EQUAL(2, microphone.calls.size());
  TEST_ASSERT_FALSE(microphone.calls[1]);
}

static void test_render_receives_the_tick_clock() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);

  app.Tick(4321);
  TEST_ASSERT_EQUAL(4321, renderer.last_render_now_ms);
  // The base dashboard keeps animating underneath a notification.
  app.Notify(Request("Alert", 1000), 9876);
  app.Tick(9876);
  TEST_ASSERT_EQUAL(9876, renderer.last_render_now_ms);
}

static void test_unknown_dashboard_without_fallback_keeps_current_selection() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  renderer.fallback_id.clear();
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  app.SelectDashboard("missing");
  TEST_ASSERT_TRUE(app.selected_dashboard().id == "text");
}

static void test_microphone_reconciles_visibility_and_avoids_duplicates() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingMicrophone microphone(&events);
  FirmwareApp app(panel, renderer, nullptr, &microphone, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  app.SelectDashboard("weather");
  app.Tick(1);
  app.Tick(2);
  TEST_ASSERT_EQUAL(1, microphone.calls.size());
  TEST_ASSERT_TRUE(microphone.calls[0]);

  // An overlay over an already-on base does not interrupt capture.
  app.Notify(Request("On", 50), 3);
  app.Tick(3);
  TEST_ASSERT_EQUAL(1, microphone.calls.size());

  // Dashboard selection remains authoritative during an overlay.
  app.SelectDashboard("text");
  TEST_ASSERT_EQUAL(2, microphone.calls.size());
  TEST_ASSERT_FALSE(microphone.calls[1]);
}

static void test_off_panel_notification_fifo_preserves_snapshot_and_black_base() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingMicrophone microphone(&events);
  FirmwareApp app(panel, renderer, nullptr, &microphone, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  app.Start(0, LightState{false, 0.4f}, "text");
  app.Notify(Request("First", 20), 1);
  app.Notify(Request("Second", 20), 1);
  app.Tick(1);  // initialize
  app.Tick(1);  // boot -> running
  app.Tick(1);  // first notification presentation
  AssertEvent(events, events.size() - 3, "content:text:First:0:black");
  AssertEvent(events, events.size() - 2, "present:force:notification");
  TEST_ASSERT_EQUAL(2u, app.overlay_queue_size());
  TEST_ASSERT_EQUAL(0, microphone.calls.size());
  // A mic-requiring dashboard selected behind a black-base overlay stays off.
  app.SelectDashboard("weather");
  TEST_ASSERT_EQUAL(0, microphone.calls.size());

  const int power_calls = panel.power_calls;
  app.Tick(21);  // promote and present the second without restoring in between
  AssertEvent(events, events.size() - 3, "content:weather:Second:0:black");
  AssertEvent(events, events.size() - 2, "present:force:notification");
  TEST_ASSERT_EQUAL(power_calls, panel.power_calls);
  app.Tick(41);
  TEST_ASSERT_FALSE(app.logical_light().on);
  AssertEvent(events, events.size() - 2, "brightness:40");
  AssertEvent(events, events.size() - 1, "power:off");
}

static void test_user_on_cancels_off_notification_without_repower_or_stale_expiry() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  app.Start(0, LightState{false, 0.4f}, "text");
  app.Notify(Request("Wake", 10), 1);
  app.Tick(1);
  app.Tick(1);
  app.Tick(1);  // successfully presented
  const int power_calls = panel.power_calls;
  const int initialize_calls = panel.initialize_calls;
  events.clear();

  app.SetUserLight(LightState{true, 0.25f}, 2);
  TEST_ASSERT_FALSE(app.notification_pending());
  TEST_ASSERT_FALSE(app.notification_visible());
  TEST_ASSERT_EQUAL(power_calls, panel.power_calls);
  TEST_ASSERT_EQUAL(initialize_calls, panel.initialize_calls);
  AssertEvent(events, 0, "brightness:25");
  app.Tick(2);
  AssertEvent(events, 1, "content:text:base");
  AssertEvent(events, 2, "present:normal:base");
  app.Tick(100);  // former notification expiry cannot restore off.
  TEST_ASSERT_TRUE(app.logical_light().on);
  TEST_ASSERT_EQUAL(power_calls, panel.power_calls);
}

static void test_notification_expiry_crosses_uint32_wrap() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  const uint32_t start = 0xFFFFFFE0u;
  StartRunning(&app, start);
  events.clear();

  const uint32_t shown = 0xFFFFFFF0u;
  NotificationRequest request = Request("Wrap", 32);
  request.has_sound = true;
  request.sound = Sound::kChirp;
  app.Notify(request, shown);
  app.Tick(shown);
  app.Tick(0x0000000Fu);
  TEST_ASSERT_TRUE(app.notification_visible());
  app.Tick(0x00000010u);
  TEST_ASSERT_FALSE(app.notification_visible());
  TEST_ASSERT_TRUE(app.logical_light().on);
  AssertEvent(events, events.size() - 5, "sound:stop");
  AssertEvent(events, events.size() - 2, "content:text:base");
  AssertEvent(events, events.size() - 1, "present:normal:base");
}

static void test_nonfinite_brightness_clamps_to_zero() {
  const float nonfinite[] = {std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity()};
  for (float brightness : nonfinite) {
    std::vector<std::string> events;
    RecordingPanel panel(&events);
    RecordingRenderer renderer(&events);
    FirmwareApp app(panel, renderer);
    app.Start(0, LightState{false, brightness}, "text");
    AssertEvent(events, 1, "brightness:0");
  }
}

static void test_cold_init_boot_and_repower_cross_uint32_wrap() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{20, 30, 40});
  const uint32_t cold = 0xFFFFFFF0u;
  app.Start(cold, LightState{true, 0.5f}, "text");
  app.Tick(0x00000003u);  // 19ms
  TEST_ASSERT_EQUAL(0, panel.initialize_calls);
  app.Tick(0x00000004u);  // 20ms
  TEST_ASSERT_EQUAL(1, panel.initialize_calls);
  app.Tick(0x0000002bu);  // 39ms boot
  TEST_ASSERT_EQUAL(1, panel.received_tokens.size());
  TEST_ASSERT_EQUAL_STRING("boot", panel.received_tokens[0].c_str());
  app.Tick(0x0000002cu);  // 40ms boot -> base
  TEST_ASSERT_EQUAL(2, panel.received_tokens.size());
  TEST_ASSERT_EQUAL_STRING("base", panel.received_tokens[1].c_str());

  app.SetUserLight(LightState{false, 0.5f}, 0x00000030u);
  app.SetUserLight(LightState{true, 0.5f}, 0xFFFFFFF0u);
  app.Tick(0x0000000du);  // 29ms after repower
  TEST_ASSERT_EQUAL(1, panel.initialize_calls);
  app.Tick(0x0000000eu);  // 30ms after repower
  TEST_ASSERT_EQUAL(2, panel.initialize_calls);
}

static void test_completed_cold_boot_does_not_replay_after_repower() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 10});

  app.Start(0, LightState{true, 0.5f}, "text");
  app.Tick(0);   // First successful initialization starts the boot animation.
  app.Tick(0);   // First boot-animation frame.
  app.Tick(10);  // Normal content after boot completes.

  app.SetUserLight(LightState{false, 0.5f}, 11);
  TEST_ASSERT_EQUAL(2, renderer.hide_base_calls);
  TEST_ASSERT_EQUAL_UINT32(11, renderer.hide_base_times.back());
  app.SetUserLight(LightState{true, 0.5f}, 12);
  app.Tick(12);  // Runtime repower initializes directly into running.
  app.Tick(12);

  TEST_ASSERT_EQUAL(2, panel.initialize_calls);
  TEST_ASSERT_EQUAL(1, CountEvent(events, "sound:boot"));
  TEST_ASSERT_EQUAL(1, CountEvent(events, "boot:0"));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kRunning),
                    static_cast<int>(app.phase()));
}

static void test_restart_hides_base_at_restart_tick() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  const int previous_hide_calls = renderer.hide_base_calls;

  TEST_ASSERT_TRUE(app.Start(123, LightState{false, 0.5f}, "text"));
  TEST_ASSERT_EQUAL(previous_hide_calls + 1, renderer.hide_base_calls);
  TEST_ASSERT_EQUAL_UINT32(123, renderer.hide_base_times.back());
}

static void test_interrupted_initial_boot_does_not_replay_after_repower() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 10});

  app.Start(0, LightState{true, 0.5f}, "text");
  app.Tick(0);  // First successful initialization enters the boot animation.
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kBootAnimation),
                    static_cast<int>(app.phase()));
  app.SetUserLight(LightState{false, 0.5f}, 1);
  app.SetUserLight(LightState{true, 0.5f}, 2);
  app.Tick(2);  // Runtime repower initializes directly into running.
  app.Tick(2);

  TEST_ASSERT_EQUAL(2, panel.initialize_calls);
  TEST_ASSERT_EQUAL(1, CountEvent(events, "sound:boot"));
  TEST_ASSERT_EQUAL(0, CountEvent(events, "boot:0"));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kRunning),
                    static_cast<int>(app.phase()));
}

static void test_start_stops_stale_sound_before_boot_lifecycle() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{10, 30, 20});

  app.Start(100, LightState{true, 0.5f}, "text");
  app.Tick(109);
  app.Tick(110);
  app.Tick(110);
  app.Tick(130);

  const std::vector<std::string> expected = {
      "sound:stop",         "resolve:text",       "brightness:50",
      "power:on",           "initialize",         "sound:boot",
      "boot:0",             "present:normal:boot", "content:text:base",
      "present:normal:base",
  };
  TEST_ASSERT_EQUAL(expected.size(), events.size());
  for (size_t i = 0; i < expected.size(); ++i)
    AssertEvent(events, i, expected[i].c_str());

  app.SetUserLight(LightState{false, 0.5f}, 140);
  app.SetUserLight(LightState{true, 0.5f}, 141);
  app.Tick(171);
  app.Tick(172);
  TEST_ASSERT_EQUAL(1, CountEvent(events, "sound:boot"));
  TEST_ASSERT_EQUAL(2, panel.initialize_calls);
}

static void test_initialize_failure_retries_before_boot() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  panel.initialize_results = {false, false, true};
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 10});

  app.Start(0, LightState{true, 0.5f}, "text");
  app.Tick(0);
  app.Tick(1);
  TEST_ASSERT_EQUAL(2, panel.initialize_calls);
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kWaitingInit),
                    static_cast<int>(app.phase()));
  TEST_ASSERT_EQUAL(0, CountEvent(events, "sound:boot"));
  TEST_ASSERT_EQUAL(0, panel.received_tokens.size());

  app.Tick(2);
  TEST_ASSERT_EQUAL(3, panel.initialize_calls);
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kBootAnimation),
                    static_cast<int>(app.phase()));
  TEST_ASSERT_EQUAL(1, CountEvent(events, "sound:boot"));
  TEST_ASSERT_EQUAL(0, panel.received_tokens.size());
  app.Tick(3);
  TEST_ASSERT_EQUAL_STRING("boot", panel.received_tokens.back().c_str());
}

static void test_notification_force_only_on_first_visible_frame() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  events.clear();

  app.Notify(Request("Static", 100), 1);
  app.Tick(1);
  AssertEvent(events, 1, "present:force:notification");
  app.Tick(34);
  AssertEvent(events, events.size() - 1, "present:normal:notification");
}

static void test_cold_start_wait_includes_panel_power_command_time() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  panel.power_on_delay_ms = 120;
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{1000, 300, 0});

  app.Start(0, LightState{true, 0.4f}, "text");
  TEST_ASSERT_EQUAL_UINT32(120, panel.LastPowerOnDelayMs());
  app.Tick(1119);
  TEST_ASSERT_EQUAL(0, panel.initialize_calls);
  app.Tick(1120);
  TEST_ASSERT_EQUAL(1, panel.initialize_calls);
}

static void test_repower_wait_includes_panel_power_command_time() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  panel.power_on_delay_ms = 120;
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{100, 300, 0});

  app.Start(0, LightState{false, 0.4f}, "text");
  app.SetUserLight(LightState{true, 0.4f}, 10);
  app.Tick(429);
  TEST_ASSERT_EQUAL(0, panel.initialize_calls);
  app.Tick(430);
  TEST_ASSERT_EQUAL(1, panel.initialize_calls);
}

static void test_start_off_uses_repower_delay_before_first_init() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{100, 30, 0});

  app.Start(0, LightState{false, 0.4f}, "text");
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kOff),
                    static_cast<int>(app.phase()));
  app.SetUserLight(LightState{true, 0.4f}, 5);
  app.Tick(34);
  TEST_ASSERT_EQUAL(0, panel.initialize_calls);
  app.Tick(35);
  TEST_ASSERT_EQUAL(1, panel.initialize_calls);
}

static void test_buttons_publish_toggle_and_full_brightness_bounce() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingLightSink sink;
  FirmwareApp app(panel, renderer, nullptr, nullptr, &sink,
                  FirmwareAppConfig{0, 0, 0});
  app.Start(0, LightState{true, 1.0f}, "text");
  app.Tick(0);
  app.Tick(0);
  events.clear();

  app.TogglePower(1);
  TEST_ASSERT_EQUAL(1, sink.published.size());
  AssertLight(sink.published[0], false, 1.0f);
  const size_t event_count_while_off = events.size();
  app.StepBrightness(2);
  TEST_ASSERT_EQUAL(event_count_while_off, events.size());
  TEST_ASSERT_EQUAL(1, sink.published.size());

  app.TogglePower(3);
  TEST_ASSERT_EQUAL(2, sink.published.size());
  AssertLight(sink.published[1], true, 1.0f);

  const float expected[] = {0.75f, 0.5f, 0.25f, 0.5f,
                            0.75f, 1.0f, 0.75f};
  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    app.StepBrightness(static_cast<uint32_t>(4 + i));
  TEST_ASSERT_EQUAL(9, sink.published.size());
  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    AssertLight(sink.published[2 + i], true, expected[i]);
  TEST_ASSERT_EQUAL_STRING("brightness:75", events.back().c_str());
}

static void test_button_edges_apply_native_gesture_policy() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingLightSink sink;
  RecordingSystem system(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, &sink,
                  FirmwareAppConfig{0, 0, 0}, &system);
  app.Start(0, LightState{true, 1.0f}, "text");

  app.PowerButtonReleased(1);  // Unmatched release.
  TEST_ASSERT_TRUE(sink.published.empty());

  app.PowerButtonPressed(10);
  app.PowerButtonReleased(60);  // Inclusive 50 ms boundary.
  TEST_ASSERT_EQUAL(1, sink.published.size());
  AssertLight(sink.published.back(), false, 1.0f);

  app.PowerButtonPressed(100);
  app.PowerButtonPressed(200);  // Duplicate edge must not restart timing.
  app.PowerButtonReleased(2101);
  TEST_ASSERT_EQUAL(1, sink.published.size());

  app.PowerButtonPressed(3000);
  app.PowerButtonReleased(13000);  // Inclusive 10 second reset boundary.
  TEST_ASSERT_EQUAL(1, system.factory_reset_calls);

  app.PowerButtonPressed(UINT32_MAX - 24);
  app.PowerButtonReleased(25);  // 50 ms across millis() rollover.
  TEST_ASSERT_EQUAL(2, sink.published.size());
  AssertLight(sink.published.back(), true, 1.0f);

  app.BrightnessButtonPressed(100);
  app.BrightnessButtonReleased(2100);  // No solar wiring: hold remains inert.
  TEST_ASSERT_EQUAL(2, sink.published.size());
  TEST_ASSERT_FALSE(app.solar_brightness_enabled());

  app.BrightnessButtonPressed(3000);
  app.BrightnessButtonReleased(3049);
  TEST_ASSERT_EQUAL(2, sink.published.size());
}

static void test_dashboard_frame_cadence_gates_render_attempts() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  renderer.catalog[0].frame_interval_ms = 50;
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  events.clear();

  app.Tick(10);
  app.Tick(49);
  TEST_ASSERT_TRUE(events.empty());
  app.Tick(50);
  AssertEvent(events, 0, "content:text:base");
  AssertEvent(events, 1, "present:normal:base");

  events.clear();
  app.Tick(99);
  TEST_ASSERT_TRUE(events.empty());
  app.Tick(100);
  AssertEvent(events, 0, "content:text:base");
}

static void test_scheduler_preserves_cadence_across_service_tick_quantization() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  renderer.catalog[0].frame_interval_ms = 33;
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  events.clear();

  for (uint32_t now_ms = 5; now_ms <= 100; now_ms += 5)
    app.Tick(now_ms);

  TEST_ASSERT_EQUAL(3, CountEvent(events, "content:text:base"));
  TEST_ASSERT_EQUAL_UINT32(100, renderer.last_render_now_ms);
}

static void test_dashboard_cadence_crosses_uint32_wrap() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  renderer.catalog[0].frame_interval_ms = 33;
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app, std::numeric_limits<uint32_t>::max() - 10);
  events.clear();

  app.Tick(21);
  TEST_ASSERT_TRUE(events.empty());
  app.Tick(22);
  AssertEvent(events, 0, "content:text:base");
}

static void test_notification_cadence_is_independent_of_base_cadence() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  renderer.catalog[0].frame_interval_ms = 50;
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);

  app.Notify(Request("Alert", 100), 1);
  app.Tick(1);
  const size_t calls_after_first_notification = renderer.render_base_calls.size();
  TEST_ASSERT_FALSE(renderer.render_base_calls.back());
  events.clear();

  app.Tick(34);
  TEST_ASSERT_EQUAL(calls_after_first_notification + 1,
                    renderer.render_base_calls.size());
  TEST_ASSERT_FALSE(renderer.render_base_calls.back());
  AssertEvent(events, 0, "content:text:Alert:33:live");
  AssertEvent(events, 1, "present:normal:notification");

  events.clear();
  app.Tick(50);
  TEST_ASSERT_TRUE(renderer.render_base_calls.back());
  TEST_ASSERT_FALSE(renderer.render_overlay_calls.back());
  AssertEvent(events, 0, "content:text:base");
}

static void test_visible_state_changes_bypass_dashboard_cadence() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  renderer.catalog[0].frame_interval_ms = 1000;
  renderer.catalog[1].frame_interval_ms = 1000;
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  events.clear();

  app.SelectDashboard("weather");
  app.Tick(1);
  AssertEvent(events, 1, "content:weather:base");

  events.clear();
  app.Notify(Request("First", 10), 2);
  app.Tick(2);
  AssertEvent(events, 0, "content:weather:First:0:live");
  app.Notify(Request("Second", 10), 3);
  app.Tick(3);
  TEST_ASSERT_EQUAL_STRING("First",
                           app.current_overlay()->notification.text.c_str());
  TEST_ASSERT_EQUAL(2u, app.overlay_queue_size());

  app.ClearOverlayQueue(4);
  app.Tick(4);
  AssertEvent(events, events.size() - 2, "content:weather:base");

  app.Notify(Request("Expire", 1), 5);
  app.Tick(5);
  app.Tick(6);
  AssertEvent(events, events.size() - 2, "content:weather:base");
}

static void test_user_on_after_off_notification_bypasses_dashboard_cadence() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  renderer.catalog[0].frame_interval_ms = 1000;
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);

  app.SetUserLight(LightState{false, 0.5f}, 1);
  app.Notify(Request("Wake", 100), 2);
  app.Tick(2);
  app.Tick(2);
  TEST_ASSERT_TRUE(app.notification_visible());

  events.clear();
  app.SetUserLight(LightState{true, 0.5f}, 3);
  app.Tick(3);
  AssertEvent(events, 1, "content:text:base");
  AssertEvent(events, 2, "present:normal:base");
}

static void test_boot_animation_uses_its_own_cadence() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 100, 33, 33});
  app.Start(0, LightState{true, 0.5f}, "text");
  app.Tick(0);
  app.Tick(10);
  app.Tick(20);
  app.Tick(30);
  TEST_ASSERT_EQUAL(1, CountEvent(events, "present:normal:boot"));
  app.Tick(43);
  TEST_ASSERT_EQUAL(2, CountEvent(events, "present:normal:boot"));
}

static void test_off_panel_notification_sound_waits_for_presentation() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  app.Start(0, LightState{false, 0.4f}, "text");
  NotificationRequest request = Request("Alert", 10);
  request.has_sound = true;
  request.sound = Sound::kSuccess;
  app.Notify(request, 1);
  TEST_ASSERT_EQUAL(0, CountEvent(events, "sound:success"));

  app.Tick(1);
  TEST_ASSERT_EQUAL(0, CountEvent(events, "sound:success"));
  app.Tick(1);
  TEST_ASSERT_TRUE(app.notification_visible());
  TEST_ASSERT_EQUAL(1, CountEvent(events, "sound:success"));
  AssertEvent(events, events.size() - 3, "present:force:notification");
  AssertEvent(events, events.size() - 2, "min-visible:Alert");
  AssertEvent(events, events.size() - 1, "sound:success");

  app.Tick(11);
  app.Notify(Request("Silent", 10), 12);
  app.Tick(12);
  app.Tick(12);
  TEST_ASSERT_TRUE(app.notification_visible());
  TEST_ASSERT_EQUAL(1, CountEvent(events, "sound:success"));
  TEST_ASSERT_EQUAL(0, CountEvent(events, "sound:chirp"));
}

static void test_notification_presentation_failures_retry_before_timer_or_sound() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);

  NotificationRequest request = Request("Render", 10);
  request.has_sound = true;
  renderer.min_visible_ms = 20;
  renderer.render_results = {false, true};
  app.Notify(request, 10);
  app.Tick(10);
  TEST_ASSERT_TRUE(app.notification_pending());
  TEST_ASSERT_EQUAL(0, CountEvent(events, "min-visible:Render"));
  TEST_ASSERT_EQUAL(0, CountEvent(events, "sound:chirp"));
  app.Tick(100);
  TEST_ASSERT_TRUE(app.notification_visible());
  app.Tick(119);
  TEST_ASSERT_TRUE(app.notification_visible());
  app.Tick(120);
  TEST_ASSERT_FALSE(app.notification_visible());

  renderer.render_results.clear();
  renderer.render_result_index = 0;
  panel.present_results = {false, true};
  panel.present_result_index = 0;
  request.notification.text = "Present";
  app.Notify(request, 200);
  app.Tick(200);
  TEST_ASSERT_TRUE(app.notification_pending());
  TEST_ASSERT_EQUAL(0, CountEvent(events, "min-visible:Present"));
  TEST_ASSERT_EQUAL(1, CountEvent(events, "sound:chirp"));
  app.Tick(300);
  TEST_ASSERT_TRUE(app.notification_visible());
  app.Tick(319);
  TEST_ASSERT_TRUE(app.notification_visible());
  app.Tick(320);
  TEST_ASSERT_FALSE(app.notification_visible());
  TEST_ASSERT_EQUAL(2, CountEvent(events, "sound:chirp"));
}

static void test_clear_overlay_queue_pending_and_visible_restores_snapshot() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 10, 0});
  app.Start(0, LightState{false, 0.4f}, "text");
  app.Notify(Request("Pending", 10), 1);
  app.ClearOverlayQueue(2);
  TEST_ASSERT_FALSE(app.notification_pending());
  TEST_ASSERT_EQUAL_UINT32(2, renderer.hide_base_times.back());
  TEST_ASSERT_EQUAL(1, renderer.release_overlay_calls);
  AssertLight(app.logical_light(), false, 0.4f);
  const int initialize_calls = panel.initialize_calls;
  app.Tick(20);
  TEST_ASSERT_EQUAL(initialize_calls, panel.initialize_calls);

  events.clear();
  app.Start(30, LightState{true, 0.4f}, "text");
  app.Tick(30);
  app.Tick(30);
  NotificationRequest visible = Request("Visible", 10);
  visible.has_sound = true;
  app.Notify(visible, 31);
  app.Tick(31);
  TEST_ASSERT_TRUE(app.notification_visible());
  app.ClearOverlayQueue(31);
  TEST_ASSERT_FALSE(app.notification_visible());
  TEST_ASSERT_EQUAL(2, renderer.release_overlay_calls);
  AssertLight(app.logical_light(), true, 0.4f);
  AssertEvent(events, events.size() - 3, "sound:stop");
  AssertEvent(events, events.size() - 2, "brightness:40");
  AssertEvent(events, events.size() - 1, "power:on");
  app.Tick(32);
  TEST_ASSERT_EQUAL_STRING("base", panel.received_tokens.back().c_str());
}

static void test_notification_queued_during_first_boot_waits_for_running() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{10, 0, 20});
  app.Start(0, LightState{true, 0.5f}, "text");
  app.Notify(Request("Wait", 10), 1);
  app.Tick(10);
  app.Tick(10);
  app.Tick(29);
  TEST_ASSERT_TRUE(app.notification_pending());
  TEST_ASSERT_EQUAL(0, CountEvent(events, "present:force:notification"));
  app.Tick(30);
  TEST_ASSERT_TRUE(app.notification_visible());
  TEST_ASSERT_EQUAL(1, CountEvent(events, "present:force:notification"));
}

static void test_reaction_queued_during_boot_renders_clean_base() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{10, 0, 20});
  app.Start(0, LightState{true, 0.5f}, "text");
  app.React(Reaction::kCelebrate, 1);
  app.Tick(10);
  app.Tick(30);
  TEST_ASSERT_TRUE(app.overlay_visible());
  TEST_ASSERT_TRUE(renderer.base_frozen_calls.back());
  TEST_ASSERT_TRUE(renderer.render_base_calls.back());
  app.Tick(63);
  TEST_ASSERT_TRUE(renderer.base_frozen_calls.back());
  TEST_ASSERT_FALSE(renderer.render_base_calls.back());
}

static void test_promoted_overlay_timer_starts_when_presented() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  app.Notify(Request("First", 10), 10);
  app.Notify(Request("Second", 20), 11);
  app.Tick(10);
  TEST_ASSERT_TRUE(app.notification_visible());

  renderer.render_results = {false, true};
  renderer.render_result_index = 0;
  app.Tick(20);  // first expires; second render fails
  TEST_ASSERT_TRUE(app.notification_pending());
  app.Tick(60);  // second first becomes visible here
  TEST_ASSERT_TRUE(app.notification_visible());
  app.Tick(79);
  TEST_ASSERT_TRUE(app.notification_visible());
  app.Tick(80);
  TEST_ASSERT_FALSE(app.notification_visible());
}

static void test_overlay_queue_rejects_new_tail_when_full() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);

  for (size_t i = 0; i < kOverlayQueueCapacity; ++i) {
    const std::string text = "item-" + std::to_string(i);
    TEST_ASSERT_TRUE(app.Notify(Request(text.c_str(), 1), 1));
  }
  TEST_ASSERT_EQUAL(kOverlayQueueCapacity, app.overlay_queue_size());
  TEST_ASSERT_FALSE(app.React(Reaction::kLove, 2));
  TEST_ASSERT_EQUAL(kOverlayQueueCapacity, app.overlay_queue_size());
  TEST_ASSERT_NOT_NULL(app.current_overlay());
  TEST_ASSERT_EQUAL_STRING("item-0",
                           app.current_overlay()->notification.text.c_str());
}

static void test_notification_text_limit_bounds_retained_queue_memory() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);

  std::string maximum(kMaximumNotificationTextBytes, 'a');
  TEST_ASSERT_TRUE(app.Notify(Request(maximum.c_str(), 1), 1));
  TEST_ASSERT_EQUAL(1u, app.overlay_queue_size());
  TEST_ASSERT_EQUAL(kMaximumNotificationTextBytes,
                    app.current_overlay()->notification.text.size());
  app.ClearOverlayQueue(1);

  NotificationRequest titled = Request("Message", 1);
  titled.notification.title = maximum;
  TEST_ASSERT_TRUE(app.Notify(std::move(titled), 1));
  TEST_ASSERT_EQUAL(1u, app.overlay_queue_size());
  TEST_ASSERT_EQUAL(kMaximumNotificationTextBytes,
                    app.current_overlay()->notification.title.size());

  std::string oversized(kMaximumNotificationTextBytes + 1, 'b');
  Notification oversized_notification{oversized, Severity::kInfo, oversized};
  TEST_ASSERT_EQUAL(kMaximumNotificationTextBytes,
                    oversized_notification.text.size());
  TEST_ASSERT_TRUE(oversized_notification.text.overflowed());
  TEST_ASSERT_EQUAL(kMaximumNotificationTextBytes,
                    oversized_notification.title.size());
  TEST_ASSERT_TRUE(oversized_notification.title.overflowed());
  TEST_ASSERT_FALSE(app.Notify(Request(oversized.c_str(), 1), 2));
  NotificationRequest oversized_title = Request("Message", 1);
  oversized_title.notification.title = oversized;
  TEST_ASSERT_TRUE(oversized_title.notification.title.overflowed());
  TEST_ASSERT_FALSE(app.Notify(std::move(oversized_title), 2));
  TEST_ASSERT_EQUAL(1u, app.overlay_queue_size());
}

static void test_notification_fifo_preserves_fixed_payloads() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  OverlayQueueStorage storage;
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0}, nullptr, nullptr, &storage);
  StartRunning(&app);

  NotificationRequest first = Request("first message", 1);
  first.notification.title = "first title";
  NotificationRequest second = Request("second message", 1);
  second.notification.title = "second title";
  TEST_ASSERT_TRUE(app.Notify(first, 1));
  TEST_ASSERT_TRUE(app.Notify(second, 1));
  TEST_ASSERT_EQUAL_STRING("first message",
                           app.current_overlay()->notification.text.c_str());
  TEST_ASSERT_EQUAL_STRING("first title",
                           app.current_overlay()->notification.title.c_str());
  TEST_ASSERT_EQUAL_STRING("first message",
                           storage.slots[0].overlay.notification.text.c_str());
  TEST_ASSERT_EQUAL_STRING("first title",
                           storage.slots[0].overlay.notification.title.c_str());

  app.Tick(1);
  app.Tick(2);
  TEST_ASSERT_EQUAL_STRING("second message",
                           app.current_overlay()->notification.text.c_str());
  TEST_ASSERT_EQUAL_STRING("second title",
                           app.current_overlay()->notification.title.c_str());
}

static void test_mixed_overlay_fifo_freezes_reaction_base_and_never_sounds_it() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  events.clear();
  sound.played.clear();

  NotificationRequest first = Request("First", 10);
  first.has_sound = true;
  first.sound = Sound::kChirp;
  TEST_ASSERT_TRUE(app.Notify(first, 1));
  TEST_ASSERT_TRUE(app.React(Reaction::kLove, 2));
  TEST_ASSERT_TRUE(app.Notify(Request("Last", 10), 3));
  app.Tick(1);
  TEST_ASSERT_EQUAL(1, CountEvent(events, "sound:chirp"));

  const int power_calls = panel.power_calls;
  app.Tick(11);
  TEST_ASSERT_EQUAL(static_cast<int>(OverlayTag::kReaction),
                    static_cast<int>(app.current_overlay()->tag));
  TEST_ASSERT_TRUE(app.overlay_visible());
  TEST_ASSERT_TRUE(renderer.base_frozen_calls.back());
  TEST_ASSERT_TRUE(renderer.render_base_calls.back());
  TEST_ASSERT_EQUAL_STRING("reaction", panel.received_tokens.back().c_str());
  TEST_ASSERT_EQUAL(1, sound.played.size());
  TEST_ASSERT_EQUAL(power_calls, panel.power_calls);

  app.Tick(2010);
  TEST_ASSERT_EQUAL(static_cast<int>(OverlayTag::kReaction),
                    static_cast<int>(app.current_overlay()->tag));
  app.Tick(2011);
  TEST_ASSERT_EQUAL(static_cast<int>(OverlayTag::kNotification),
                    static_cast<int>(app.current_overlay()->tag));
  TEST_ASSERT_EQUAL_STRING("Last",
                           app.current_overlay()->notification.text.c_str());
  TEST_ASSERT_TRUE(renderer.render_base_calls.back());
  TEST_ASSERT_EQUAL_STRING("notification", panel.received_tokens.back().c_str());
  TEST_ASSERT_EQUAL(power_calls, panel.power_calls);
}

static void test_reaction_duration_crosses_uint32_wrap_without_sound() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app, 0xFFFFFFE0u);
  const int stop_calls = sound.stop_calls;
  sound.played.clear();

  TEST_ASSERT_TRUE(app.React(Reaction::kCelebrate, 0xFFFFFFF0u));
  app.Tick(0xFFFFFFF0u);
  app.Tick(0x00000887u);
  TEST_ASSERT_TRUE(app.overlay_visible());
  app.Tick(0x00000888u);
  TEST_ASSERT_FALSE(app.overlay_visible());
  TEST_ASSERT_TRUE(sound.played.empty());
  TEST_ASSERT_EQUAL(stop_calls, sound.stop_calls);
}

static void test_clear_and_explicit_off_cancel_the_entire_overlay_queue() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  app.React(Reaction::kApprove, 1);
  app.Notify(Request("Tail", 10), 2);
  app.Tick(1);
  app.ClearOverlayQueue(2);
  TEST_ASSERT_EQUAL(0u, app.overlay_queue_size());
  AssertLight(app.logical_light(), true, 0.5f);

  app.Notify(Request("First", 10), 3);
  app.React(Reaction::kCrying, 4);
  app.Tick(3);
  app.SetUserLight(LightState{false, 0.5f}, 4);
  TEST_ASSERT_EQUAL(0u, app.overlay_queue_size());
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kOff),
                    static_cast<int>(app.phase()));
}

static void test_dashboard_selection_during_on_panel_notification_uses_live_base() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingMicrophone microphone(&events);
  FirmwareApp app(panel, renderer, nullptr, &microphone, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  app.Notify(Request("Overlay", 20), 1);
  app.Tick(1);
  TEST_ASSERT_TRUE(app.notification_visible());
  app.SelectDashboard("weather");
  TEST_ASSERT_EQUAL(1, microphone.calls.size());
  TEST_ASSERT_TRUE(microphone.calls[0]);
  app.Tick(2);
  TEST_ASSERT_EQUAL_STRING("content:weather:Overlay:1:live",
                           events[events.size() - 2].c_str());
}

static void test_start_rejects_invalid_initial_dashboard_safely() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingMicrophone microphone(&events);
  renderer.fallback_id.clear();
  FirmwareApp app(panel, renderer, nullptr, &microphone, nullptr,
                  FirmwareAppConfig{0, 0, 0});

  TEST_ASSERT_FALSE(app.Start(0, LightState{true, 1.5f}, "missing"));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kOff),
                    static_cast<int>(app.phase()));
  TEST_ASSERT_TRUE(app.selected_dashboard().id.empty());
  AssertLight(app.logical_light(), true, 1.0f);
  AssertEvent(events, 0, "resolve:missing");
  AssertEvent(events, 1, "brightness:100");
  AssertEvent(events, 2, "power:off");
  TEST_ASSERT_EQUAL(0, microphone.calls.size());
}

static void test_start_rejects_empty_resolved_dashboard_safely() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  renderer.catalog.push_back({"", false});
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});

  TEST_ASSERT_FALSE(app.Start(0, LightState{true, 0.5f}, ""));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kOff),
                    static_cast<int>(app.phase()));
  TEST_ASSERT_TRUE(app.selected_dashboard().id.empty());
  AssertEvent(events, 0, "resolve:");
  AssertEvent(events, 1, "brightness:50");
  AssertEvent(events, 2, "power:off");
}

static void test_restart_invalid_dashboard_clears_running_selection_safely() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingMicrophone microphone(&events);
  FirmwareApp app(panel, renderer, nullptr, &microphone, nullptr,
                  FirmwareAppConfig{0, 0, 0});

  TEST_ASSERT_TRUE(app.Start(0, LightState{true, 0.5f}, "text"));
  app.Tick(0);
  app.Tick(0);
  app.SelectDashboard("weather");
  TEST_ASSERT_TRUE(microphone.calls.back());
  renderer.fallback_id.clear();
  events.clear();

  TEST_ASSERT_FALSE(app.Start(1, LightState{true, -1.0f}, "missing"));
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kOff),
                    static_cast<int>(app.phase()));
  TEST_ASSERT_TRUE(app.selected_dashboard().id.empty());
  AssertLight(app.logical_light(), true, 0.0f);
  AssertEvent(events, 0, "microphone:off");
  AssertEvent(events, 1, "resolve:missing");
  AssertEvent(events, 2, "brightness:0");
  AssertEvent(events, 3, "power:off");
  TEST_ASSERT_FALSE(microphone.calls.back());
}

static void test_start_brightness_bounce_syncs_from_25_percent() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingLightSink sink;
  FirmwareApp app(panel, renderer, nullptr, nullptr, &sink,
                  FirmwareAppConfig{0, 0, 0});

  TEST_ASSERT_TRUE(app.Start(0, LightState{true, 0.25f}, "text"));
  app.StepBrightness(1);
  TEST_ASSERT_EQUAL(1, sink.published.size());
  AssertLight(sink.published[0], true, 0.5f);
}

static void test_external_brightness_syncs_bounce_from_25_percent() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingLightSink sink;
  FirmwareApp app(panel, renderer, nullptr, nullptr, &sink,
                  FirmwareAppConfig{0, 0, 0});

  TEST_ASSERT_TRUE(app.Start(0, LightState{true, 1.0f}, "text"));
  app.SetUserLight(LightState{true, 0.25f}, 1);
  app.StepBrightness(2);
  TEST_ASSERT_EQUAL(1, sink.published.size());
  AssertLight(sink.published[0], true, 0.5f);
}

static void test_same_brightness_echo_preserves_bounce_direction() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingLightSink sink;
  FirmwareApp app(panel, renderer, nullptr, nullptr, &sink,
                  FirmwareAppConfig{0, 0, 0});

  TEST_ASSERT_TRUE(app.Start(0, LightState{true, 1.0f}, "text"));
  app.StepBrightness(1);
  app.SetUserLight(LightState{true, 0.75f}, 2);
  app.StepBrightness(3);
  TEST_ASSERT_EQUAL(2, sink.published.size());
  AssertLight(sink.published[0], true, 0.75f);
  AssertLight(sink.published[1], true, 0.5f);
}

static void test_fifo_stops_head_sound_and_waits_for_next_presentation() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  events.clear();

  NotificationRequest first = Request("First", 20);
  first.has_sound = true;
  first.sound = Sound::kChirp;
  app.Notify(first, 10);
  app.Tick(10);
  TEST_ASSERT_EQUAL(1, CountEvent(events, "sound:chirp"));

  NotificationRequest second = Request("Second", 20);
  second.has_sound = true;
  second.sound = Sound::kSuccess;
  app.Notify(second, 11);
  TEST_ASSERT_EQUAL(0, CountEvent(events, "sound:stop"));
  TEST_ASSERT_EQUAL(2u, app.overlay_queue_size());

  panel.present_results = {false, true};
  panel.present_result_index = 0;
  app.Tick(30);
  TEST_ASSERT_TRUE(app.notification_pending());
  TEST_ASSERT_EQUAL(1, CountEvent(events, "sound:stop"));
  TEST_ASSERT_EQUAL(0, CountEvent(events, "sound:success"));
  app.Tick(63);
  TEST_ASSERT_TRUE(app.notification_visible());
  TEST_ASSERT_EQUAL(1, CountEvent(events, "sound:success"));
  AssertEvent(events, events.size() - 1, "sound:success");
  TEST_ASSERT_EQUAL(2, sound.stop_calls);
}

static void test_pending_notification_cancellation_does_not_stop_unrelated_sound() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  const int stop_calls_before = sound.stop_calls;
  events.clear();

  NotificationRequest pending = Request("Pending", 20);
  pending.has_sound = true;
  app.Notify(pending, 10);
  TEST_ASSERT_TRUE(app.notification_pending());
  app.Notify(Request("Silent tail", 20), 11);
  TEST_ASSERT_TRUE(app.notification_pending());
  app.ClearOverlayQueue(11);

  TEST_ASSERT_EQUAL(stop_calls_before, sound.stop_calls);
  TEST_ASSERT_EQUAL(0, CountEvent(events, "sound:stop"));
}

static void test_presented_silent_notification_preserves_unrelated_sound_when_on() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  const int stop_calls_before = sound.stop_calls;
  events.clear();

  app.Notify(Request("Silent", 20), 10);
  app.Tick(10);
  TEST_ASSERT_TRUE(app.notification_visible());
  app.ClearOverlayQueue(10);

  TEST_ASSERT_TRUE(app.logical_light().on);
  TEST_ASSERT_EQUAL(stop_calls_before, sound.stop_calls);
  TEST_ASSERT_EQUAL(0, CountEvent(events, "sound:stop"));
}

static void test_notification_expiry_stops_sound_before_off_restore() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  app.Start(0, LightState{false, 0.4f}, "text");
  events.clear();

  NotificationRequest request = Request("Expiry", 10);
  request.has_sound = true;
  app.Notify(request, 1);
  app.Tick(1);
  app.Tick(1);
  TEST_ASSERT_TRUE(app.notification_visible());
  events.clear();

  app.Tick(11);
  AssertEvent(events, 0, "sound:stop");
  AssertEvent(events, 1, "brightness:40");
  AssertEvent(events, 2, "power:off");
  TEST_ASSERT_EQUAL(2, sound.stop_calls);
}

static void test_pending_off_notification_stops_boot_sound_before_power_restore() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  app.Start(0, LightState{false, 0.4f}, "text");
  app.Notify(Request("Pending", 20), 1);
  app.Tick(1);
  TEST_ASSERT_TRUE(app.notification_pending());
  TEST_ASSERT_EQUAL(1, CountEvent(events, "sound:boot"));
  events.clear();

  app.ClearOverlayQueue(2);
  TEST_ASSERT_FALSE(app.logical_light().on);
  TEST_ASSERT_EQUAL_UINT32(2, renderer.hide_base_times.back());
  AssertEvent(events, 0, "brightness:40");
  AssertEvent(events, 1, "sound:stop");
  AssertEvent(events, 2, "power:off");
}

static void test_silent_off_notification_stops_boot_sound_before_restore() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  app.Start(0, LightState{false, 0.4f}, "text");
  app.Notify(Request("Silent", 20), 1);
  app.Tick(1);
  app.Tick(1);
  TEST_ASSERT_TRUE(app.notification_visible());
  events.clear();

  app.ClearOverlayQueue(2);
  TEST_ASSERT_EQUAL_UINT32(2, renderer.hide_base_times.back());
  AssertEvent(events, 0, "brightness:40");
  AssertEvent(events, 1, "sound:stop");
  AssertEvent(events, 2, "power:off");
  TEST_ASSERT_EQUAL(2, sound.stop_calls);
}

static void test_brightness_only_preserves_overlay_sound_and_timer() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  NotificationRequest request = Request("Keep", 20);
  request.has_sound = true;
  app.Notify(request, 1);
  app.Tick(1);
  events.clear();

  app.SetUserLight(LightState{true, 0.75f}, 2);
  AssertEvent(events, 0, "brightness:75");
  TEST_ASSERT_EQUAL(0, CountEvent(events, "sound:stop"));
  TEST_ASSERT_TRUE(app.notification_visible());
  app.Tick(20);
  TEST_ASSERT_TRUE(app.notification_visible());
  app.Tick(21);
  TEST_ASSERT_FALSE(app.notification_visible());
  AssertLight(app.logical_light(), true, 0.75f);
}

static void test_off_state_brightness_change_preserves_temporary_wake() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  app.Start(0, LightState{false, 0.4f}, "text");
  app.Notify(Request("Wake", 20), 1);
  app.Tick(1);
  app.Tick(1);
  TEST_ASSERT_TRUE(app.notification_visible());
  const int power_calls = panel.power_calls;

  app.SetUserLight(LightState{false, 0.7f}, 2);
  TEST_ASSERT_TRUE(app.notification_visible());
  TEST_ASSERT_EQUAL(power_calls, panel.power_calls);
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kRunning),
                    static_cast<int>(app.phase()));
  app.StepBrightness(3);
  TEST_ASSERT_TRUE(app.notification_visible());
  AssertLight(app.logical_light(), false, 1.0f);
  app.Tick(21);
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kOff),
                    static_cast<int>(app.phase()));
  AssertLight(app.logical_light(), false, 1.0f);
  TEST_ASSERT_EQUAL(power_calls + 1, panel.power_calls);
}

static void test_power_commands_cancel_a_temporary_off_state_wake() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  app.Start(0, LightState{false, 0.4f}, "text");

  app.React(Reaction::kLove, 1);
  app.Tick(1);
  app.Tick(1);
  TEST_ASSERT_TRUE(app.overlay_visible());
  app.SetUserLight(LightState{false, 0.4f}, 2);
  TEST_ASSERT_EQUAL(0u, app.overlay_queue_size());
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kOff),
                    static_cast<int>(app.phase()));

  app.React(Reaction::kLove, 3);
  app.Tick(3);
  app.Tick(3);
  TEST_ASSERT_TRUE(app.overlay_visible());
  app.TogglePower(4);
  TEST_ASSERT_EQUAL(0u, app.overlay_queue_size());
  TEST_ASSERT_EQUAL(static_cast<int>(FirmwareApp::Phase::kOff),
                    static_cast<int>(app.phase()));
}

static void test_logical_off_stops_sound_before_power_off() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  events.clear();

  app.SetUserLight(LightState{false, 0.5f}, 1);
  AssertEvent(events, 0, "brightness:50");
  AssertEvent(events, 1, "sound:stop");
  AssertEvent(events, 2, "power:off");
  TEST_ASSERT_EQUAL(2, sound.stop_calls);
}

static void test_reboot_uses_typed_system_port() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  RecordingSystem system(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{}, &system);
  app.Reboot();
  TEST_ASSERT_EQUAL(1, sound.stop_calls);
  TEST_ASSERT_EQUAL(1, system.reboot_calls);
  TEST_ASSERT_EQUAL(0, system.factory_reset_calls);
  AssertEvent(events, 0, "sound:stop");
  AssertEvent(events, 1, "system:reboot");
}

static void test_factory_reset_uses_typed_system_port() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  RecordingSystem system(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{}, &system);
  app.FactoryReset();
  TEST_ASSERT_EQUAL(1, sound.stop_calls);
  TEST_ASSERT_EQUAL(0, system.reboot_calls);
  TEST_ASSERT_EQUAL(1, system.factory_reset_calls);
  AssertEvent(events, 0, "sound:stop");
  AssertEvent(events, 1, "system:factory-reset");
}

static void test_inbound_user_light_never_publishes_to_sink() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingLightSink sink;
  FirmwareApp app(panel, renderer, nullptr, nullptr, &sink,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  app.SetUserLight(LightState{false, 0.5f}, 1);
  app.SetUserLight(LightState{true, 0.25f}, 2);
  TEST_ASSERT_EQUAL(0, sink.published.size());
}

// FrameOutput presentation policy: brightness scaling, change suppression, and
// resend cadence. Consumed by the panel adapter, not by FirmwareApp.
// Raw FrameOutput helper: configures capacity to the source size and prepares
// into a caller-owned buffer, the only path production uses.
static bool RawPrepare(FrameOutput &output, const std::vector<uint8_t> &source,
                       float brightness, uint32_t now_ms,
                       uint32_t min_interval_ms, std::vector<uint8_t> &transmit,
                       bool force = false) {
  return output.Prepare(source.data(), source.size(), brightness, now_ms,
                        min_interval_ms, transmit.data(), transmit.size(),
                        force);
}

static void test_frame_output_scales_and_suppresses_static_frames() {
  FrameOutput output;
  const std::vector<uint8_t> source = {100, 200, 255};
  std::vector<uint8_t> transmit(source.size());
  TEST_ASSERT_TRUE(output.ConfigureCapacity(source.size()));
  // now=0, generous interval so only change detection is exercised.
  TEST_ASSERT_TRUE(RawPrepare(output, source, 0.5f, 0, 1000, transmit));
  const std::vector<uint8_t> want = {50, 100, 128};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want.data(), transmit.data(), want.size());
  // Same frame, well within the interval -> suppressed.
  TEST_ASSERT_FALSE(RawPrepare(output, source, 0.5f, 10, 1000, transmit));
}

static void test_frame_output_raw_prepare_requires_configured_capacity() {
  FrameOutput output;
  const std::vector<uint8_t> source = {10, 20, 30};
  std::vector<uint8_t> sentinel(source.size(), 0xCC);
  TEST_ASSERT_FALSE(output.ConfigureCapacity(kFramePayloadBytes + 1));
  TEST_ASSERT_FALSE(output.Prepare(source.data(), 0, 1.0f, 0, 1000,
                                   sentinel.data(), sentinel.size()));
  TEST_ASSERT_FALSE(output.Prepare(source.data(), source.size(), 1.0f, 0, 1000,
                                   sentinel.data(), sentinel.size()));
  TEST_ASSERT_TRUE(output.ConfigureCapacity(source.size() - 1));
  TEST_ASSERT_FALSE(output.Prepare(source.data(), source.size(), 1.0f, 0, 1000,
                                   sentinel.data(), sentinel.size()));
  TEST_ASSERT_FALSE(output.Prepare(source.data(), source.size(), 1.0f, 0, 1000,
                                   nullptr, sentinel.size()));
  for (uint8_t byte : sentinel) TEST_ASSERT_EQUAL_UINT8(0xCC, byte);

  TEST_ASSERT_TRUE(output.ConfigureCapacity(source.size()));
  const size_t retained_capacity = output.RetainedCapacity();
  TEST_ASSERT_TRUE(output.Prepare(source.data(), source.size(), 1.0f, 0, 1000,
                                  sentinel.data(), sentinel.size()));
  TEST_ASSERT_EQUAL_UINT(retained_capacity, output.RetainedCapacity());
  output.Reset();
  TEST_ASSERT_EQUAL_UINT(retained_capacity, output.RetainedCapacity());
  TEST_ASSERT_TRUE(output.Prepare(source.data(), source.size(), 1.0f, 1, 1000,
                                  sentinel.data(), sentinel.size()));
  TEST_ASSERT_EQUAL_UINT(retained_capacity, output.RetainedCapacity());
}

static void test_frame_output_raw_prepare_supports_full_payload() {
  std::vector<uint8_t> source(kFramePayloadBytes);
  for (size_t index = 0; index < source.size(); ++index)
    source[index] = static_cast<uint8_t>(index);
  std::vector<uint8_t> transaction(kFramePayloadBytes + 5, 0xCC);
  FrameOutput output;

  TEST_ASSERT_TRUE(output.ConfigureCapacity(kFramePayloadBytes));
  TEST_ASSERT_TRUE(output.Prepare(source.data(), source.size(), 0.5f, 0, 1000,
                                  transaction.data() + 4,
                                  transaction.size() - 4));
  TEST_ASSERT_EQUAL_UINT8(0, transaction[4]);
  TEST_ASSERT_EQUAL_UINT8(1, transaction[5]);
  TEST_ASSERT_EQUAL_UINT8(128, transaction[4 + 255]);

  std::vector<uint8_t> oversized(kFramePayloadBytes + 1, 0x55);
  TEST_ASSERT_FALSE(output.Prepare(oversized.data(), oversized.size(), 1.0f, 1,
                                   1000, transaction.data() + 4,
                                   transaction.size() - 4));
}

static void test_frame_output_nonfinite_brightness_blacks_frame() {
  const float values[] = {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()};
  const std::vector<uint8_t> source = {100, 200, 255};
  for (float brightness : values) {
    FrameOutput output;
    std::vector<uint8_t> transmit(source.size());
    TEST_ASSERT_TRUE(output.ConfigureCapacity(source.size()));
    TEST_ASSERT_TRUE(RawPrepare(output, source, brightness, 0, 1000, transmit));
    const std::vector<uint8_t> black = {0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(black.data(), transmit.data(), black.size());
  }
}

static void test_frame_output_forces_unchanged_frame() {
  FrameOutput output;
  const std::vector<uint8_t> source = {1, 2, 3};
  std::vector<uint8_t> transmit(source.size());
  TEST_ASSERT_TRUE(output.ConfigureCapacity(source.size()));
  TEST_ASSERT_TRUE(RawPrepare(output, source, 1.0f, 0, 1000, transmit));
  TEST_ASSERT_TRUE(RawPrepare(output, source, 1.0f, 1, 1000, transmit, true));
}

static void test_frame_output_reset_allows_first_frame_again() {
  FrameOutput output;
  const std::vector<uint8_t> source = {1, 2, 3};
  std::vector<uint8_t> transmit(source.size());
  TEST_ASSERT_TRUE(output.ConfigureCapacity(source.size()));
  TEST_ASSERT_TRUE(RawPrepare(output, source, 1.0f, 0, 1000, transmit));
  TEST_ASSERT_FALSE(RawPrepare(output, source, 1.0f, 1, 1000, transmit));
  output.Reset();
  TEST_ASSERT_TRUE(RawPrepare(output, source, 1.0f, 1, 1000, transmit));
}

static void test_frame_output_resends_unchanged_after_interval() {
  FrameOutput output;
  const std::vector<uint8_t> source = {1, 2, 3};
  std::vector<uint8_t> transmit(source.size());
  TEST_ASSERT_TRUE(output.ConfigureCapacity(source.size()));
  TEST_ASSERT_TRUE(RawPrepare(output, source, 1.0f, 0, 1000, transmit));
  // Unchanged and before the interval elapses -> suppressed.
  TEST_ASSERT_FALSE(RawPrepare(output, source, 1.0f, 999, 1000, transmit));
  // Unchanged but the refresh floor elapsed -> resend.
  TEST_ASSERT_TRUE(RawPrepare(output, source, 1.0f, 1000, 1000, transmit));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(source.data(), transmit.data(), source.size());
  // Send resets the clock: next unchanged call within the interval suppresses.
  TEST_ASSERT_FALSE(RawPrepare(output, source, 1.0f, 1500, 1000, transmit));
}

static void test_frame_output_blanks_once_while_off() {
  FrameOutput output;
  const std::vector<uint8_t> visible = {10, 20, 30};
  const std::vector<uint8_t> changed_while_off = {40, 50, 60};
  std::vector<uint8_t> transmit(visible.size());
  TEST_ASSERT_TRUE(output.ConfigureCapacity(visible.size()));
  TEST_ASSERT_TRUE(RawPrepare(output, visible, 1.0f, 0, 1000, transmit));
  TEST_ASSERT_TRUE(RawPrepare(output, visible, 0.0f, 10, 1000, transmit));
  const std::vector<uint8_t> black = {0, 0, 0};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(black.data(), transmit.data(), black.size());
  TEST_ASSERT_FALSE(
      RawPrepare(output, changed_while_off, 0.0f, 20, 1000, transmit));
  TEST_ASSERT_TRUE(
      RawPrepare(output, changed_while_off, 1.0f, 30, 1000, transmit));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(changed_while_off.data(), transmit.data(),
                                changed_while_off.size());
}

// SoundPlayer port: a fake recorder proves consumers drive sound through the
// abstract interface without the buzzer or tone backend.
class FakeSoundPlayer : public SoundPlayer {
 public:
  void Play(Sound sound) override { played.push_back(sound); }
  void Stop() override { stops++; }

  std::vector<Sound> played;
  int stops = 0;
};

static void request_boot(SoundPlayer &player) { player.Play(Sound::kBoot); }

static void test_play_records_named_sound() {
  FakeSoundPlayer player;
  request_boot(player);
  TEST_ASSERT_EQUAL(1, player.played.size());
  TEST_ASSERT_TRUE(player.played[0] == Sound::kBoot);
}

static void test_latest_play_appends() {
  FakeSoundPlayer player;
  player.Play(Sound::kPling1);
  player.Play(Sound::kAlarm1);
  TEST_ASSERT_EQUAL(2, player.played.size());
  TEST_ASSERT_TRUE(player.played[1] == Sound::kAlarm1);
}

static void test_stop_counts() {
  FakeSoundPlayer player;
  player.Stop();
  TEST_ASSERT_EQUAL(1, player.stops);
}

static void test_timing_precision_pause_fresh_start_cap_and_reveal() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  const int hides = renderer.hide_base_calls;
  const std::string base = app.selected_dashboard().id;
  app.StopwatchStart(100);
  app.Tick(1234);
  TEST_ASSERT_EQUAL_UINT32(1134, app.timing().value_ms);
  TEST_ASSERT_EQUAL_UINT32(1134, renderer.last_timing.value_ms);
  app.TimingPause(1240);
  app.Tick(2000);
  TEST_ASSERT_TRUE(app.timing().active());
  TEST_ASSERT_FALSE(app.timing().running());
  TEST_ASSERT_EQUAL_UINT32(1140, app.timing().value_ms);
  app.TimingResume(2000);
  app.TimingPause(2055);
  TEST_ASSERT_EQUAL_UINT32(1195, app.timing().value_ms);
  app.StopwatchStart(3000);
  TEST_ASSERT_EQUAL_UINT32(0, app.timing().value_ms);
  app.Tick(3000 + kStopwatchMaximumElapsedMs + 1);
  TEST_ASSERT_EQUAL_UINT32(kStopwatchMaximumElapsedMs, app.timing().value_ms);
  TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kPaused);
  app.TimingResume(3000 + kStopwatchMaximumElapsedMs + 2);
  TEST_ASSERT_FALSE(app.timing().running());
  app.TimingStop(3000 + kStopwatchMaximumElapsedMs + 3);
  TEST_ASSERT_FALSE(app.timing().active());
  TEST_ASSERT_FALSE(app.timing().visible());
  TEST_ASSERT_TRUE(app.timing().kind == TimingKind::kStopwatch);
  TEST_ASSERT_EQUAL_UINT32(kStopwatchMaximumElapsedMs, app.timing().value_ms);
  TEST_ASSERT_EQUAL(hides, renderer.hide_base_calls);
  TEST_ASSERT_EQUAL_STRING(base.c_str(), app.selected_dashboard().id.c_str());
  TEST_ASSERT_EQUAL(0, std::count(sound.played.begin(), sound.played.end(), Sound::kAlarm1));
}

static void test_timing_temporary_wake_user_override_and_off_wrap() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingLightSink sink;
  FirmwareApp app(panel, renderer, nullptr, nullptr, &sink,
                  FirmwareAppConfig{0, 0, 0});
  TEST_ASSERT_TRUE(app.Start(0xfffffff0u, LightState{false, 0.5f}, "text"));
  app.StopwatchStart(0xfffffff0u);
  TEST_ASSERT_TRUE(panel.power_on);
  TEST_ASSERT_FALSE(app.logical_light().on);
  TEST_ASSERT_TRUE(sink.published.empty());
  app.Tick(0xfffffff0u);
  app.Tick(0x20u);
  TEST_ASSERT_EQUAL_UINT32(48, app.timing().value_ms);
  app.TogglePower(0x21u);
  TEST_ASSERT_FALSE(panel.power_on);
  app.Tick(0x30u);
  TEST_ASSERT_EQUAL_UINT32(64, app.timing().value_ms);
  app.TogglePower(0x31u);
  TEST_ASSERT_TRUE(app.logical_light().on);
  app.Tick(0x31u);
  app.TimingStop(0x40u);
  TEST_ASSERT_TRUE(panel.power_on);
  app.SetUserLight(LightState{false, 0.5f}, 100);
  app.StopwatchStart(110);
  app.Tick(110);
  app.TimingStop(120);
  TEST_ASSERT_FALSE(panel.power_on);
}

static void test_timing_replacement_shared_controls_invalid_and_restart() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  app.StopwatchStart(10);
  app.Tick(50);
  TEST_ASSERT_FALSE(app.TimerStart(6000, 100));
  TEST_ASSERT_FALSE(app.TimerStart(-1, 100));
  TEST_ASSERT_EQUAL_UINT32(40, app.timing().value_ms);
  app.TimingPause(100);
  TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kPaused);
  TEST_ASSERT_EQUAL_UINT32(90, app.timing().value_ms);
  app.TimingResume(100);
  TEST_ASSERT_TRUE(app.timing().running());
  TEST_ASSERT_TRUE(app.TimerStart(2, 100));
  app.Tick(600);
  TEST_ASSERT_EQUAL_UINT32(1500, app.timing().value_ms);
  app.TimingPause(700);
  app.Tick(900);
  TEST_ASSERT_EQUAL_UINT32(1400, app.timing().value_ms);
  app.TimingResume(1000);
  app.Tick(1101);
  TEST_ASSERT_EQUAL_UINT32(1299, app.timing().value_ms);
  app.TimerStart(2, 1200);
  TEST_ASSERT_EQUAL_UINT32(2000, app.timing().value_ms);
  app.TimingStop(1300);
  TEST_ASSERT_EQUAL_UINT32(1900, app.timing().value_ms);
  TEST_ASSERT_EQUAL_UINT32(2000, app.timing().duration_ms);
  TEST_ASSERT_FALSE(app.timing().active());
  app.TimingResume(1400);
  TEST_ASSERT_FALSE(app.timing().active());
  app.StopwatchStart(1500);
  TEST_ASSERT_EQUAL_UINT32(0, app.timing().duration_ms);
  TEST_ASSERT_TRUE(app.Start(1600, LightState{false, 0.5f}, "text"));
  TEST_ASSERT_TRUE(app.timing().kind == TimingKind::kNone);
}

static void test_timer_expiry_preempts_overlay_wakes_latches_and_bounds_alarm() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app, 0xfffffff0u);
  app.TimerStart(1, 0xfffffff0u);
  app.SetUserLight(LightState{false, 0.5f}, 0xfffffff1u);
  NotificationRequest overlay = Request("cover", 10000);
  overlay.has_sound = true;
  app.Notify(overlay, 0xfffffff2u);
  app.Tick(0xfffffff2u);
  app.Tick(0xfffffff3u);
  app.Tick(984u);
  TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kFinished);
  TEST_ASSERT_EQUAL_UINT32(0, app.timing().value_ms);
  TEST_ASSERT_EQUAL_UINT32(1000, app.timing().duration_ms);
  TEST_ASSERT_EQUAL(0, app.overlay_queue_size());
  TEST_ASSERT_TRUE(panel.power_on);
  TEST_ASSERT_FALSE(app.logical_light().on);
  TEST_ASSERT_TRUE(sound.played.back() == Sound::kAlarm1);
  TEST_ASSERT_FALSE(app.Notify(overlay, 985u));
  TEST_ASSERT_FALSE(app.React(Reaction::kLove, 985u));
  const size_t plays = sound.played.size();
  app.Tick(3983u);
  TEST_ASSERT_EQUAL(plays, sound.played.size());
  app.Tick(3984u);
  TEST_ASSERT_EQUAL(plays + 1, sound.played.size());
  app.Tick(30984u);
  const size_t bounded_plays = sound.played.size();
  const int stops = sound.stop_calls;
  app.Tick(60984u);
  TEST_ASSERT_EQUAL(bounded_plays, sound.played.size());
  TEST_ASSERT_EQUAL(stops, sound.stop_calls);
  TEST_ASSERT_TRUE(app.timing().active());
  app.TimingStop(60985u);
  TEST_ASSERT_FALSE(app.timing().active());
  TEST_ASSERT_FALSE(panel.power_on);
}

static void test_timing_boot_suppression_and_overlay_restore() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{100, 0, 5000});
  TEST_ASSERT_TRUE(app.Start(0, LightState{true, 0.5f}, "text"));
  app.StopwatchStart(1);
  app.Tick(100);
  TEST_ASSERT_TRUE(app.phase() == FirmwareApp::Phase::kRunning);
  TEST_ASSERT_EQUAL(0, std::count(sound.played.begin(), sound.played.end(), Sound::kBoot));
  app.Tick(101);
  const int hides = renderer.hide_base_calls;
  app.React(Reaction::kLove, 102);
  app.Tick(102);
  app.ClearOverlayQueue(103);
  app.Tick(103);
  TEST_ASSERT_TRUE(renderer.last_timing.active());
  TEST_ASSERT_TRUE(renderer.render_base_calls.back());
  TEST_ASSERT_EQUAL(hides, renderer.hide_base_calls);
  app.Notify(Request("cover", 1000), 104);
  app.TimerStart(1, 105);
  TEST_ASSERT_EQUAL(0, app.overlay_queue_size());
  app.Tick(105);
  TEST_ASSERT_TRUE(renderer.last_timing.kind == TimingKind::kTimer);
  TEST_ASSERT_TRUE(app.Start(200, LightState{true, 0.5f}, "text"));
  app.Tick(300);
  TEST_ASSERT_TRUE(app.phase() == FirmwareApp::Phase::kBootAnimation);
  const int initialized = panel.initialize_calls;
  const int stops = sound.stop_calls;
  app.StopwatchStart(301);
  TEST_ASSERT_TRUE(app.phase() == FirmwareApp::Phase::kRunning);
  TEST_ASSERT_EQUAL(stops + 1, sound.stop_calls);
  TEST_ASSERT_EQUAL(initialized, panel.initialize_calls);
}

static void test_timing_brightness_gestures_capture_context() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSolarBrightnessSink solar;
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0}, nullptr, nullptr, nullptr, &solar);
  StartRunning(&app);
  app.StopwatchStart(10);
  app.BrightnessButtonPressed(100);
  app.BrightnessButtonReleased(150);
  TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kPaused);
  app.BrightnessButtonPressed(200);
  app.BrightnessButtonReleased(2199);
  TEST_ASSERT_TRUE(app.timing().running());
  app.BrightnessButtonPressed(2200);
  app.TimerStart(5, 2300);
  app.BrightnessButtonReleased(4200);
  TEST_ASSERT_TRUE(app.timing().running());
  TEST_ASSERT_TRUE(solar.published.empty());
  app.BrightnessButtonPressed(4300);
  app.TimerStart(5, 4400);
  app.BrightnessButtonReleased(6300);
  TEST_ASSERT_TRUE(app.timing().active());
  app.BrightnessButtonPressed(6400);
  app.TimingStop(6500);
  app.BrightnessButtonReleased(8400);
  TEST_ASSERT_TRUE(solar.published.empty());
  app.TimerStart(1, 8500);
  app.BrightnessButtonPressed(8600);
  app.Tick(9500);
  app.BrightnessButtonReleased(9550);
  TEST_ASSERT_FALSE(app.timing().active());
  app.StopwatchStart(10000);
  app.BrightnessButtonPressed(10100);
  app.BrightnessButtonReleased(12100);
  TEST_ASSERT_FALSE(app.timing().active());
  TEST_ASSERT_TRUE(solar.published.empty());
}

static void test_timing_wake_brightness_and_overlay_do_not_reinitialize() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingLightSink light;
  FirmwareApp app(panel, renderer, nullptr, nullptr, &light,
                  FirmwareAppConfig{0, 0, 0});
  TEST_ASSERT_TRUE(app.Start(0, LightState{false, 0.4f}, "text",
                             SolarBrightnessConfig{true, 0.8f, 0.2f}));
  app.React(Reaction::kLove, 10);
  app.Tick(10);
  app.Tick(11);
  const int powers = panel.power_calls;
  const int initialized = panel.initialize_calls;
  app.StopwatchStart(12);
  TEST_ASSERT_EQUAL(powers, panel.power_calls);
  TEST_ASSERT_EQUAL(initialized, panel.initialize_calls);
  TEST_ASSERT_EQUAL(0, app.overlay_queue_size());
  TEST_ASSERT_EQUAL_STRING("brightness:40", events.back().c_str());
  app.Tick(12);
  const int hides = renderer.hide_base_calls;
  app.React(Reaction::kLove, 13);
  app.Tick(13);
  TEST_ASSERT_EQUAL(powers, panel.power_calls);
  TEST_ASSERT_EQUAL(initialized, panel.initialize_calls);
  app.SetSolarElevation(-6.0f, 14);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, app.logical_light().brightness);
  TEST_ASSERT_EQUAL_STRING("brightness:20", events.back().c_str());
  app.ClearOverlayQueue(15);
  TEST_ASSERT_TRUE(panel.power_on);
  TEST_ASSERT_EQUAL(hides, renderer.hide_base_calls);
  app.Tick(15);
  TEST_ASSERT_TRUE(renderer.last_timing.active());
  TEST_ASSERT_TRUE(renderer.render_base_calls.back());
  app.SetUserLight(LightState{false, 0.6f}, 16);
  TEST_ASSERT_TRUE(panel.power_on);
  TEST_ASSERT_TRUE(app.timing().running());
  TEST_ASSERT_FALSE(app.solar_brightness_enabled());
  app.SetUserLight(LightState{true, 0.6f}, 17);
  TEST_ASSERT_EQUAL(powers, panel.power_calls);
  app.TimingStop(18);
  TEST_ASSERT_TRUE(panel.power_on);
}

static void test_timing_cadence_microphone_and_overlay_dto() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingMicrophone microphone(&events);
  renderer.catalog = {{"text", true, 1000}};
  FirmwareApp app(panel, renderer, nullptr, &microphone, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  app.StopwatchStart(10);
  app.Tick(10);
  TEST_ASSERT_FALSE(microphone.calls.back());
  const size_t renders = renderer.render_base_calls.size();
  app.Tick(42);
  TEST_ASSERT_EQUAL(renders, renderer.render_base_calls.size());
  app.Tick(43);
  TEST_ASSERT_EQUAL(renders + 1, renderer.render_base_calls.size());
  app.Notify(Request("cover", 1000), 44);
  app.Tick(44);
  TEST_ASSERT_TRUE(renderer.last_timing.kind == TimingKind::kStopwatch);
  TEST_ASSERT_EQUAL_UINT32(34, renderer.last_timing.value_ms);
  app.TimingPause(45);
  app.Tick(45);
  TEST_ASSERT_TRUE(renderer.last_timing.phase == TimingPhase::kPaused);
  app.TimingStop(46);
  app.Tick(46);
  TEST_ASSERT_TRUE(microphone.calls.back());
  TEST_ASSERT_FALSE(renderer.last_timing.active());
  const size_t base_renders = renderer.render_base_calls.size();
  app.Tick(79);
  TEST_ASSERT_EQUAL(base_renders, renderer.render_base_calls.size());
}

static void test_timer_alarm_full_repeat_window_and_matching_sound_owner() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  app.TimerStart(1, 10);
  app.Tick(1010);
  const int stops = sound.stop_calls;
  app.TimingPause(1011);
  app.TimingResume(1011);
  TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kFinished);
  TEST_ASSERT_EQUAL(stops, sound.stop_calls);
  for (uint32_t now = 4010; now < 31010; now += 3000)
    app.Tick(now);
  TEST_ASSERT_EQUAL(10, std::count(sound.played.begin(), sound.played.end(), Sound::kAlarm1));
  app.Tick(31010);
  TEST_ASSERT_EQUAL(stops + 1, sound.stop_calls);
  TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kFinished);
  app.TimerStart(1, 32000);
  app.Tick(33000);
  const int before_replace = sound.stop_calls;
  app.StopwatchStart(33001);
  TEST_ASSERT_EQUAL(before_replace + 1, sound.stop_calls);
  app.Tick(40000);
  TEST_ASSERT_TRUE(app.timing().kind == TimingKind::kStopwatch);
}

static void test_expired_timer_power_off_cancels_alarm_repeats() {
  for (bool initially_on : {false, true}) {
    std::vector<std::string> events;
    RecordingPanel panel(&events);
    RecordingRenderer renderer(&events);
    RecordingSound sound(&events);
    FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                    FirmwareAppConfig{0, 0, 0});
    TEST_ASSERT_TRUE(app.Start(0, LightState{initially_on, 0.4f}, "text"));
    TEST_ASSERT_TRUE(app.TimerStart(1, 10));
    app.Tick(10);
    app.Tick(1010);
    const size_t plays = sound.played.size();
    TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kFinished);
    app.SetUserLight(LightState{false, 0.4f}, 1011);
    for (uint32_t now : {4010u, 10010u, 31010u, 40000u})
      app.Tick(now);
    TEST_ASSERT_FALSE(panel.power_on);
    TEST_ASSERT_EQUAL(plays, sound.played.size());

    // Turning the screen off before expiry does not cancel the countdown.
    TEST_ASSERT_TRUE(app.TimerStart(1, 41000));
    app.SetUserLight(LightState{false, 0.4f}, 41100);
    app.Tick(42000);
    TEST_ASSERT_TRUE(panel.power_on);
    TEST_ASSERT_EQUAL(plays + 1, sound.played.size());
  }
}

static void test_overdue_timer_stop_does_not_wake_or_announce_expiry() {
  for (bool initially_on : {false, true}) {
    for (bool hide : {false, true}) {
      std::vector<std::string> events;
      RecordingPanel panel(&events);
      RecordingRenderer renderer(&events);
      RecordingSound sound(&events);
      FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                      FirmwareAppConfig{0, 0, 0});
      TEST_ASSERT_TRUE(app.Start(0, LightState{initially_on, 0.4f}, "text"));
      TEST_ASSERT_TRUE(app.TimerStart(1, 10));
      app.Tick(10);
      if (hide)
        app.SetUserLight(LightState{false, 0.4f}, 20);
      const auto power_ons = std::count(events.begin(), events.end(), "power:on");
      const size_t plays = sound.played.size();
      app.TimingStop(1011);
      TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kIdle);
      TEST_ASSERT_EQUAL_UINT32(0, app.timing().value_ms);
      TEST_ASSERT_EQUAL(power_ons, std::count(events.begin(), events.end(), "power:on"));
      TEST_ASSERT_EQUAL(plays, sound.played.size());
      TEST_ASSERT_EQUAL(initially_on && !hide, panel.power_on);
    }
  }
}

static void test_timer_hidden_expiry_wakes_without_sound_player() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  app.TimerStart(1, 100);
  app.SetUserLight(LightState{false, 0.4f}, 200);
  TEST_ASSERT_FALSE(panel.power_on);
  const int powers = panel.power_calls;
  app.Tick(1000);
  TEST_ASSERT_EQUAL_UINT32(100, app.timing().value_ms);
  TEST_ASSERT_FALSE(panel.power_on);
  app.Tick(1100);
  TEST_ASSERT_TRUE(panel.power_on);
  TEST_ASSERT_EQUAL(powers + 1, panel.power_calls);
  TEST_ASSERT_FALSE(app.logical_light().on);
  TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kFinished);
  app.Tick(1101);
  TEST_ASSERT_TRUE(renderer.last_timing.phase == TimingPhase::kFinished);
  app.TimingStop(1200);
  TEST_ASSERT_FALSE(panel.power_on);
}

static void test_invalid_timer_start_preserves_overlay_and_finished_alarm() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSound sound(&events);
  FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  app.StopwatchStart(10);
  app.Notify(Request("cover", 10000), 20);
  app.Tick(20);
  const size_t event_count = events.size();
  TEST_ASSERT_FALSE(app.TimerStart(0, 500));
  TEST_ASSERT_EQUAL(event_count, events.size());
  TEST_ASSERT_TRUE(app.overlay_visible());
  TEST_ASSERT_EQUAL_UINT32(10, app.timing().value_ms);
  app.TimerStart(1, 600);
  app.Tick(1600);
  const int stops = sound.stop_calls;
  const size_t plays = sound.played.size();
  TEST_ASSERT_FALSE(app.TimerStart(6000, 2000));
  TEST_ASSERT_EQUAL(stops, sound.stop_calls);
  TEST_ASSERT_EQUAL(plays, sound.played.size());
  TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kFinished);
  app.Tick(4600);
  TEST_ASSERT_EQUAL(plays + 1, sound.played.size());
}

static void test_timing_button_overlay_context_and_no_closed_session_fallthrough() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingSolarBrightnessSink solar;
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0}, nullptr, nullptr, nullptr, &solar);
  StartRunning(&app);
  app.StopwatchStart(10);
  app.Notify(Request("cover", 10000), 20);
  app.Tick(20);
  const float brightness = app.logical_light().brightness;
  app.BrightnessButtonPressed(30);
  app.BrightnessButtonReleased(80);
  TEST_ASSERT_TRUE(app.timing().running());
  TEST_ASSERT_TRUE(brightness != app.logical_light().brightness);
  app.ClearOverlayQueue(90);
  app.BrightnessButtonPressed(100);
  app.TimingStop(101);
  app.BrightnessButtonReleased(2100);
  TEST_ASSERT_TRUE(solar.published.empty());
  app.StopwatchStart(2200);
  app.BrightnessButtonPressed(2300);
  app.BrightnessButtonReleased(12300);
  TEST_ASSERT_FALSE(app.timing().active());
  TEST_ASSERT_TRUE(solar.published.empty());
}

static void test_timing_minimize_preserves_session_and_dashboard_policy() {
  for (bool timer : {false, true}) {
    std::vector<std::string> events;
    RecordingPanel panel(&events);
    RecordingRenderer renderer(&events);
    RecordingMicrophone microphone(&events);
    renderer.catalog = {{"text", true, 1000}, {"other", false, 500}};
    FirmwareApp app(panel, renderer, nullptr, &microphone, nullptr,
                    FirmwareAppConfig{0, 0, 0});
    StartRunning(&app);
    if (timer) app.TimerStart(10, 10); else app.StopwatchStart(10);
    app.TimingMinimize(110);
    TEST_ASSERT_TRUE(app.timing().active());
    TEST_ASSERT_TRUE(app.timing().running());
    TEST_ASSERT_FALSE(app.timing().visible());
    TEST_ASSERT_TRUE(microphone.calls.back());
    app.Tick(210);
    TEST_ASSERT_EQUAL_UINT32(timer ? 9800 : 200, app.timing().value_ms);
    TEST_ASSERT_TRUE(renderer.last_timing.minimized);
    const size_t renders = renderer.render_base_calls.size();
    app.Tick(243);
    TEST_ASSERT_EQUAL(renders, renderer.render_base_calls.size());
    app.Tick(1210);
    TEST_ASSERT_EQUAL(renders + 1, renderer.render_base_calls.size());
    app.TimingShow(1220);
    app.Tick(1220);
    TEST_ASSERT_TRUE(app.timing().visible());
    TEST_ASSERT_FALSE(microphone.calls.back());
    TEST_ASSERT_EQUAL_UINT32(timer ? 8790 : 1210, app.timing().value_ms);
    app.TimingPause(1230);
    const uint32_t paused = app.timing().value_ms;
    app.TimingMinimize(1240);
    app.SelectDashboard("other");
    app.Tick(1300);
    TEST_ASSERT_FALSE(microphone.calls.back());
    TEST_ASSERT_EQUAL_STRING("other", app.selected_dashboard().id.c_str());
    app.TimingShow(1400);
    TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kPaused);
    TEST_ASSERT_EQUAL_UINT32(paused, app.timing().value_ms);
    app.TimingMinimize(1500);
    app.Tick(1600);
    TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kPaused);
    TEST_ASSERT_EQUAL_UINT32(paused, app.timing().value_ms);
    app.TimingResume(1610);
    app.Tick(1620);
    TEST_ASSERT_TRUE(app.timing().running());
    TEST_ASSERT_EQUAL_UINT32(timer ? paused - 10 : paused + 10,
                             app.timing().value_ms);
    app.TimingStop(1630);
    TEST_ASSERT_FALSE(app.timing().visible());
  }
}

static void test_timing_minimize_idle_and_idempotence() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  size_t count = events.size();
  app.TimingPause(1); app.TimingResume(1); app.TimingStop(1);
  app.TimingMinimize(1); app.TimingShow(1);
  TEST_ASSERT_EQUAL(count, events.size());
  app.StopwatchStart(10);
  app.TimingMinimize(20); app.TimingShow(20);
  TEST_ASSERT_TRUE(app.timing().visible());
  TEST_ASSERT_EQUAL_UINT32(10, app.timing().value_ms);
  app.TimingMinimize(30);
  count = events.size();
  app.TimingMinimize(40);
  TEST_ASSERT_EQUAL(count, events.size());
  app.TimingShow(50);
  const int initializes = panel.initialize_calls;
  app.TimingShow(60);
  TEST_ASSERT_EQUAL(initializes, panel.initialize_calls);
  TEST_ASSERT_EQUAL_UINT32(50, app.timing().value_ms);
  app.TimingStop(70);
  TEST_ASSERT_FALSE(app.timing().visible());
  count = events.size();
  app.TimingPause(80); app.TimingResume(80); app.TimingStop(80);
  app.TimingShow(80); app.TimingMinimize(80);
  TEST_ASSERT_EQUAL(count, events.size());
  app.TimerStart(10, 100);
  app.TimingMinimize(110); app.TimingShow(110);
  TEST_ASSERT_TRUE(app.timing().visible());
  TEST_ASSERT_EQUAL_UINT32(9990, app.timing().value_ms);
  app.TimingMinimize(120);
  app.TimerStart(10, 130);
  TEST_ASSERT_TRUE(app.timing().visible());
  app.TimingStop(140);
  TEST_ASSERT_FALSE(app.timing().visible());
  count = events.size();
  app.TimingPause(150); app.TimingResume(150); app.TimingStop(150);
  app.TimingShow(150); app.TimingMinimize(150);
  TEST_ASSERT_EQUAL(count, events.size());
}

static void test_timing_minimize_off_wake_and_overlay_restoration() {
  for (bool reaction : {false, true}) {
    std::vector<std::string> events;
    RecordingPanel panel(&events);
    RecordingRenderer renderer(&events);
    RecordingLightSink sink;
    FirmwareApp app(panel, renderer, nullptr, nullptr, &sink,
                    FirmwareAppConfig{0, 0, 0});
    TEST_ASSERT_TRUE(app.Start(0, LightState{false, 0.4f}, "text"));
    app.StopwatchStart(10);
    app.Tick(10);
    app.TimingPause(20);
    app.TimingMinimize(30);
    TEST_ASSERT_FALSE(panel.power_on);
    TEST_ASSERT_TRUE(app.phase() == FirmwareApp::Phase::kOff);
    TEST_ASSERT_FALSE(app.logical_light().on);
    app.TimingShow(40);
    app.Tick(40);
    TEST_ASSERT_TRUE(panel.power_on);
    TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kPaused);
    TEST_ASSERT_EQUAL_UINT32(10, app.timing().value_ms);
    if (reaction) app.React(Reaction::kLove, 50);
    else app.Notify(Request("cover", 1000), 50);
    app.Tick(50);
    const int powers = panel.power_calls;
    app.TimingMinimize(60);
    TEST_ASSERT_TRUE(panel.power_on);
    TEST_ASSERT_EQUAL(powers, panel.power_calls);
    TEST_ASSERT_EQUAL(1, app.overlay_queue_size());
    TEST_ASSERT_EQUAL_STRING("brightness:100", events.back().c_str());
    app.Tick(60);
    TEST_ASSERT_FALSE(renderer.base_visible_calls.back());
    TEST_ASSERT_FALSE(renderer.last_timing.visible());
    app.ClearOverlayQueue(70);
    TEST_ASSERT_FALSE(panel.power_on);
    TEST_ASSERT_FALSE(app.logical_light().on);
    if (reaction) app.React(Reaction::kLove, 80);
    else app.Notify(Request("cover", 1000), 80);
    app.Tick(80);
    app.Tick(81);
    const int initialized = panel.initialize_calls;
    app.TimingShow(90);
    TEST_ASSERT_EQUAL(initialized, panel.initialize_calls);
    TEST_ASSERT_EQUAL(0, app.overlay_queue_size());
    TEST_ASSERT_TRUE(panel.power_on);
    TEST_ASSERT_EQUAL_STRING("brightness:40", events.back().c_str());
    TEST_ASSERT_TRUE(sink.published.empty());
    app.SetUserLight(LightState{true, 0.4f}, 100);
    app.TimingMinimize(110);
    TEST_ASSERT_TRUE(panel.power_on);
    TEST_ASSERT_TRUE(app.logical_light().on);
  }
}

static void test_minimized_timer_completion_foreground_and_alarm() {
  for (bool initially_on : {false, true}) {
    std::vector<std::string> events;
    RecordingPanel panel(&events);
    RecordingRenderer renderer(&events);
    RecordingSound sound(&events);
    FirmwareApp app(panel, renderer, &sound, nullptr, nullptr,
                    FirmwareAppConfig{0, 0, 0});
    TEST_ASSERT_TRUE(app.Start(0, LightState{initially_on, 0.4f}, "text"));
    app.TimerStart(1, 10);
    app.Tick(10);
    app.TimingMinimize(20);
    app.Notify(Request("cover", 10000), 30);
    app.Tick(30);
    app.Tick(31);
    app.TimingMinimize(1010);
    app.Tick(1010);
    TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kFinished);
    TEST_ASSERT_TRUE(app.timing().visible());
    TEST_ASSERT_TRUE(panel.power_on);
    TEST_ASSERT_EQUAL(0, app.overlay_queue_size());
    TEST_ASSERT_TRUE(sound.played.back() == Sound::kAlarm1);
    const size_t count = events.size();
    app.TimingMinimize(1020);
    TEST_ASSERT_EQUAL(count, events.size());
    app.Tick(4010);
    TEST_ASSERT_EQUAL(2, std::count(sound.played.begin(), sound.played.end(), Sound::kAlarm1));
    app.TimingStop(4020);
    TEST_ASSERT_FALSE(app.timing().visible());
    TEST_ASSERT_EQUAL(initially_on, panel.power_on);
    // A minimize command at the deadline must not hide completion either.
    app.TimerStart(1, 5000);
    app.TimingMinimize(6000);
    TEST_ASSERT_TRUE(app.timing().visible());
    TEST_ASSERT_TRUE(app.timing().phase == TimingPhase::kFinished);
  }
}

static void test_timing_minimize_button_contexts_and_live_overlays() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  StartRunning(&app);
  app.StopwatchStart(10);
  app.BrightnessButtonPressed(20);
  app.TimingMinimize(30);
  const float brightness = app.logical_light().brightness;
  app.BrightnessButtonReleased(80);
  TEST_ASSERT_TRUE(app.timing().running());
  TEST_ASSERT_EQUAL_FLOAT(brightness, app.logical_light().brightness);
  app.BrightnessButtonPressed(100);
  app.BrightnessButtonReleased(150);
  TEST_ASSERT_TRUE(app.timing().running());
  TEST_ASSERT_TRUE(brightness != app.logical_light().brightness);
  const float adjusted = app.logical_light().brightness;
  app.BrightnessButtonPressed(200);
  app.TimingShow(210);
  app.BrightnessButtonReleased(250);
  TEST_ASSERT_TRUE(app.timing().running());
  TEST_ASSERT_EQUAL_FLOAT(adjusted, app.logical_light().brightness);
  app.BrightnessButtonPressed(300);
  app.TimingMinimize(310);
  app.TimingShow(320);
  app.BrightnessButtonReleased(350);
  TEST_ASSERT_TRUE(app.timing().running());
  app.React(Reaction::kLove, 400);
  app.Tick(400);
  app.TimingMinimize(410);
  app.Tick(410);
  TEST_ASSERT_TRUE(renderer.render_base_calls.back());
  TEST_ASSERT_TRUE(renderer.base_visible_calls.back());
  TEST_ASSERT_FALSE(renderer.last_timing.visible());
  TEST_ASSERT_EQUAL(1, app.overlay_queue_size());
  app.ClearOverlayQueue(420);
  app.Notify(Request("cover", 1000), 430);
  app.TimingShow(440);
  app.Notify(Request("cover", 1000), 450);
  app.Tick(450);
  app.TimingMinimize(460);
  app.Tick(460);
  TEST_ASSERT_TRUE(renderer.render_base_calls.back());
  TEST_ASSERT_TRUE(renderer.render_overlay_calls.back());
  TEST_ASSERT_FALSE(renderer.last_timing.visible());
}

static void test_light_publication_guard_consumes_only_the_expected_echo() {
  LightStatePublicationGuard guard;
  TEST_ASSERT_FALSE(guard.ConsumeIfExpected(LightState{true, 0.4f}));

  guard.Expect(LightState{false, 0.2f});
  TEST_ASSERT_TRUE(guard.pending());
  TEST_ASSERT_TRUE(guard.ConsumeIfExpected(LightState{false, 0.2f}));
  TEST_ASSERT_FALSE(guard.pending());
  TEST_ASSERT_FALSE(guard.ConsumeIfExpected(LightState{false, 0.2f}));

  guard.Expect(LightState{true, 0.6f});
  TEST_ASSERT_FALSE(guard.ConsumeIfExpected(LightState{true, 0.7f}));
  TEST_ASSERT_FALSE(guard.pending());

  guard.Expect(LightState{true, 0.3f});
  guard.Expect(LightState{true, 0.8f});
  TEST_ASSERT_TRUE(guard.ConsumeIfExpected(LightState{true, 0.8f}));
}

static void test_solar_brightness_interpolates_clamps_levels_and_invalid_input() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingLightSink light_sink;
  FirmwareApp app(panel, renderer, nullptr, nullptr, &light_sink,
                  FirmwareAppConfig{0, 0, 0});
  TEST_ASSERT_TRUE(app.Start(0, LightState{true, 0.4f}, "text",
                             SolarBrightnessConfig{true, 2.0f, 0.01f}));
  TEST_ASSERT_TRUE(app.SolarElevationDue(0));

  app.SetSolarElevation(std::numeric_limits<float>::quiet_NaN(), 0);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.4f, app.logical_light().brightness);
  TEST_ASSERT_TRUE(light_sink.published.empty());
  TEST_ASSERT_FALSE(app.SolarElevationDue(999));
  TEST_ASSERT_TRUE(app.SolarElevationDue(1000));

  app.SetSolarElevation(-7.0f, 1000);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.05f,
                           app.logical_light().brightness);
  app.SetSolarElevation(0.0f, 1001);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.525f,
                           app.logical_light().brightness);
  app.SetSolarBrightnessLevels(0.8f, 0.4f, 1002);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.6f,
                           app.logical_light().brightness);
  const size_t publications_after_levels = light_sink.published.size();
  app.SetSolarBrightnessLevels(0.8f, 0.4f, 1003);
  TEST_ASSERT_EQUAL(publications_after_levels, light_sink.published.size());
  app.SetSolarElevation(7.0f, 1004);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.8f,
                           app.logical_light().brightness);
  TEST_ASSERT_EQUAL(4, light_sink.published.size());
  for (bool persistent : light_sink.persistent_publications)
    TEST_ASSERT_FALSE(persistent);
}

static void test_solar_elevation_cadence_is_wrap_safe() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  const uint32_t valid_at = std::numeric_limits<uint32_t>::max() - 500;
  FirmwareApp app(panel, renderer, nullptr, nullptr, nullptr,
                  FirmwareAppConfig{0, 0, 0});
  TEST_ASSERT_TRUE(app.Start(valid_at, LightState{true, 0.4f}, "text",
                             SolarBrightnessConfig{true, 1.0f, 0.2f}));
  app.SetSolarElevation(0.0f, valid_at);
  TEST_ASSERT_FALSE(app.SolarElevationDue(valid_at + 59999u));
  TEST_ASSERT_TRUE(app.SolarElevationDue(valid_at + 60000u));

  const uint32_t invalid_at = std::numeric_limits<uint32_t>::max() - 100;
  app.SetSolarElevation(std::numeric_limits<float>::quiet_NaN(), invalid_at);
  TEST_ASSERT_FALSE(app.SolarElevationDue(invalid_at + 999u));
  TEST_ASSERT_TRUE(app.SolarElevationDue(invalid_at + 1000u));
}

static void test_solar_mode_manual_power_and_overlay_policy() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingLightSink light_sink;
  RecordingSolarBrightnessSink solar_sink;
  FirmwareApp app(panel, renderer, nullptr, nullptr, &light_sink,
                  FirmwareAppConfig{0, 0, 0}, nullptr, nullptr, nullptr,
                  &solar_sink);
  TEST_ASSERT_TRUE(app.Start(0, LightState{true, 0.4f}, "text",
                             SolarBrightnessConfig{true, 0.8f, 0.2f}));
  app.SetSolarElevation(6.0f, 1);
  events.clear();
  const int power_calls = panel.power_calls;
  app.SetSolarElevation(-6.0f, 2);
  TEST_ASSERT_EQUAL(power_calls, panel.power_calls);
  TEST_ASSERT_FALSE(light_sink.persistent_publications.back());

  // A power-only HA update preserves the mode and automatic level.
  const size_t publications_before_user_input = light_sink.published.size();
  app.SetUserLight(LightState{false, 0.2f}, 3);
  TEST_ASSERT_TRUE(app.solar_brightness_enabled());
  TEST_ASSERT_FALSE(app.logical_light().on);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.2f,
                           app.logical_light().brightness);
  TEST_ASSERT_EQUAL(publications_before_user_input, light_sink.published.size());

  // The active HA light call owns its requested value; the core only mirrors
  // the switch change and must not publish a stale automatic light state.
  app.SetUserLight(LightState{false, 0.6f}, 4);
  TEST_ASSERT_FALSE(app.solar_brightness_enabled());
  TEST_ASSERT_EQUAL(1, solar_sink.published.size());
  TEST_ASSERT_FALSE(solar_sink.published.back());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.6f,
                           app.logical_light().brightness);
  TEST_ASSERT_EQUAL(publications_before_user_input, light_sink.published.size());

  // Disabling from the Solar switch freezes and persists the effective level.
  app.SetSolarBrightnessEnabled(true, 5);
  app.SetSolarElevation(6.0f, 5);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.8f,
                           app.logical_light().brightness);
  app.SetSolarBrightnessEnabled(false, 6);
  TEST_ASSERT_FALSE(app.solar_brightness_enabled());
  TEST_ASSERT_TRUE(light_sink.persistent_publications.back());
  AssertLight(light_sink.published.back(), false, 0.8f);
  TEST_ASSERT_EQUAL(1, solar_sink.published.size());

  // An automatic update behind an off-panel overlay retains the existing
  // temporary full-brightness wake, then restores the latest solar level.
  app.SetSolarBrightnessEnabled(true, 7);
  app.SetSolarElevation(6.0f, 7);
  app.React(Reaction::kApprove, 8);
  const size_t wake_events = events.size();
  app.SetSolarElevation(-6.0f, 9);
  TEST_ASSERT_EQUAL(wake_events, events.size());
  app.ClearOverlayQueue(10);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.2f,
                           app.logical_light().brightness);
  TEST_ASSERT_FALSE(panel.power_on);
}

static void test_solar_brightness_button_gestures_and_off_state() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingLightSink light_sink;
  RecordingSolarBrightnessSink solar_sink;
  FirmwareApp app(panel, renderer, nullptr, nullptr, &light_sink,
                  FirmwareAppConfig{0, 0, 0}, nullptr, nullptr, nullptr,
                  &solar_sink);
  TEST_ASSERT_TRUE(app.Start(0, LightState{false, 0.4f}, "text",
                             SolarBrightnessConfig{true, 1.0f, 0.2f}));
  app.SetSolarElevation(6.0f, 1);
  const int power_calls_while_off = panel.power_calls;

  app.BrightnessButtonPressed(10);
  app.BrightnessButtonReleased(59);  // Below short-press boundary.
  app.BrightnessButtonPressed(100);
  app.BrightnessButtonReleased(150);  // Short presses remain inert while off.
  TEST_ASSERT_TRUE(app.solar_brightness_enabled());
  TEST_ASSERT_TRUE(solar_sink.published.empty());

  app.BrightnessButtonPressed(200);
  app.BrightnessButtonReleased(2200);  // Exactly two seconds toggles off.
  TEST_ASSERT_FALSE(app.solar_brightness_enabled());
  TEST_ASSERT_TRUE(light_sink.persistent_publications.back());
  app.BrightnessButtonPressed(3000);
  app.BrightnessButtonReleased(13000);  // Inclusive ten seconds toggles on.
  TEST_ASSERT_TRUE(app.solar_brightness_enabled());
  app.BrightnessButtonPressed(14000);
  app.BrightnessButtonReleased(24001);  // Longer holds do nothing.
  TEST_ASSERT_TRUE(app.solar_brightness_enabled());
  TEST_ASSERT_EQUAL(2, solar_sink.published.size());
  TEST_ASSERT_EQUAL(power_calls_while_off, panel.power_calls);

  // Once on, a short press takes over from the effective solar level and
  // persists the stepped manual brightness.
  app.SetUserLight(LightState{true, app.logical_light().brightness}, 25000);
  app.SetSolarElevation(-6.0f, 25001);
  app.BrightnessButtonPressed(26000);
  app.BrightnessButtonReleased(26050);
  TEST_ASSERT_FALSE(app.solar_brightness_enabled());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f,
                           app.logical_light().brightness);
  TEST_ASSERT_TRUE(light_sink.persistent_publications.back());
  TEST_ASSERT_EQUAL(3, solar_sink.published.size());

  app.BrightnessButtonPressed(27000);
  app.BrightnessButtonReleased(28999);  // 1,999 ms remains a short press.
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.75f,
                           app.logical_light().brightness);
}

static void test_light_publication_echo_does_not_cancel_an_off_overlay() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingLightSink light_sink;
  FirmwareApp app(panel, renderer, nullptr, nullptr, &light_sink,
                  FirmwareAppConfig{0, 0, 0});
  TEST_ASSERT_TRUE(app.Start(0, LightState{false, 0.4f}, "text",
                             SolarBrightnessConfig{true, 0.8f, 0.2f}));
  app.SetSolarElevation(6.0f, 1);
  TEST_ASSERT_EQUAL(1, light_sink.published.size());

  LightStatePublicationGuard guard;
  guard.Expect(light_sink.published.back());
  TEST_ASSERT_TRUE(app.React(Reaction::kApprove, 2));
  TEST_ASSERT_EQUAL(1u, app.overlay_queue_size());
  TEST_ASSERT_TRUE(panel.power_on);

  const LightState publication_echo = light_sink.published.back();
  if (!guard.ConsumeIfExpected(publication_echo))
    app.SetUserLight(publication_echo, 3);
  TEST_ASSERT_EQUAL(1u, app.overlay_queue_size());
  TEST_ASSERT_TRUE(panel.power_on);

  // The same state received again is an explicit external off command and
  // retains the existing overlay-cancellation behavior.
  if (!guard.ConsumeIfExpected(publication_echo))
    app.SetUserLight(publication_echo, 4);
  TEST_ASSERT_EQUAL(0u, app.overlay_queue_size());
  TEST_ASSERT_FALSE(panel.power_on);
}

static void test_power_button_keeps_its_two_second_short_boundary() {
  std::vector<std::string> events;
  RecordingPanel panel(&events);
  RecordingRenderer renderer(&events);
  RecordingLightSink light_sink;
  FirmwareApp app(panel, renderer, nullptr, nullptr, &light_sink,
                  FirmwareAppConfig{0, 0, 0});
  TEST_ASSERT_TRUE(app.Start(0, LightState{true, 1.0f}, "text"));

  app.PowerButtonPressed(100);
  app.PowerButtonReleased(2100);
  TEST_ASSERT_FALSE(app.logical_light().on);
  TEST_ASSERT_EQUAL(1, light_sink.published.size());
}

// ---- in/ (spectrum) tests ----


int main(int, char **) {
  UNITY_BEGIN();
  RunDashboardRestoreTests();
  RUN_TEST(test_play_records_named_sound);
  RUN_TEST(test_latest_play_appends);
  RUN_TEST(test_stop_counts);
  RUN_TEST(test_frame_output_scales_and_suppresses_static_frames);
  RUN_TEST(test_frame_output_raw_prepare_requires_configured_capacity);
  RUN_TEST(test_frame_output_raw_prepare_supports_full_payload);
  RUN_TEST(test_frame_output_nonfinite_brightness_blacks_frame);
  RUN_TEST(test_frame_output_forces_unchanged_frame);
  RUN_TEST(test_frame_output_reset_allows_first_frame_again);
  RUN_TEST(test_frame_output_resends_unchanged_after_interval);
  RUN_TEST(test_frame_output_blanks_once_while_off);
  RUN_TEST(test_notification_duration_seconds_convert_to_milliseconds);
  RUN_TEST(test_timer_seconds_reject_invalid_duration);
  RUN_TEST(test_timezone_catalog_maps_labels_to_posix_with_valid_default);
  RUN_TEST(test_severity_parse_roundtrips_and_falls_back);
  RUN_TEST(test_reaction_parse_roundtrips_and_has_designed_durations);
  RUN_TEST(test_frame_handoff_uses_renderer_views_exactly);
  RUN_TEST(test_firmware_update_presents_static_frame_synchronously);
  RUN_TEST(test_firmware_update_skips_unready_or_failed_presentation);
  RUN_TEST(test_frame_metrics_window_uses_elapsed_wall_time);
  RUN_TEST(test_regular_frame_metrics_cover_render_through_present_only);
  RUN_TEST(test_invalid_or_failed_frame_is_not_presented);
  RUN_TEST(test_dashboard_metadata_is_trusted_not_caller_supplied);
  RUN_TEST(test_render_receives_the_tick_clock);
  RUN_TEST(test_unknown_dashboard_without_fallback_keeps_current_selection);
  RUN_TEST(test_microphone_reconciles_visibility_and_avoids_duplicates);
  RUN_TEST(test_off_panel_notification_fifo_preserves_snapshot_and_black_base);
  RUN_TEST(test_user_on_cancels_off_notification_without_repower_or_stale_expiry);
  RUN_TEST(test_notification_expiry_crosses_uint32_wrap);
  RUN_TEST(test_nonfinite_brightness_clamps_to_zero);
  RUN_TEST(test_cold_init_boot_and_repower_cross_uint32_wrap);
  RUN_TEST(test_completed_cold_boot_does_not_replay_after_repower);
  RUN_TEST(test_restart_hides_base_at_restart_tick);
  RUN_TEST(test_interrupted_initial_boot_does_not_replay_after_repower);
  RUN_TEST(test_start_stops_stale_sound_before_boot_lifecycle);
  RUN_TEST(test_initialize_failure_retries_before_boot);
  RUN_TEST(test_notification_force_only_on_first_visible_frame);
  RUN_TEST(test_cold_start_wait_includes_panel_power_command_time);
  RUN_TEST(test_repower_wait_includes_panel_power_command_time);
  RUN_TEST(test_start_off_uses_repower_delay_before_first_init);
  RUN_TEST(test_buttons_publish_toggle_and_full_brightness_bounce);
  RUN_TEST(test_button_edges_apply_native_gesture_policy);
  RUN_TEST(test_light_publication_guard_consumes_only_the_expected_echo);
  RUN_TEST(test_solar_brightness_interpolates_clamps_levels_and_invalid_input);
  RUN_TEST(test_solar_elevation_cadence_is_wrap_safe);
  RUN_TEST(test_solar_mode_manual_power_and_overlay_policy);
  RUN_TEST(test_solar_brightness_button_gestures_and_off_state);
  RUN_TEST(test_light_publication_echo_does_not_cancel_an_off_overlay);
  RUN_TEST(test_power_button_keeps_its_two_second_short_boundary);
  RUN_TEST(test_dashboard_frame_cadence_gates_render_attempts);
  RUN_TEST(test_scheduler_preserves_cadence_across_service_tick_quantization);
  RUN_TEST(test_dashboard_cadence_crosses_uint32_wrap);
  RUN_TEST(test_notification_cadence_is_independent_of_base_cadence);
  RUN_TEST(test_visible_state_changes_bypass_dashboard_cadence);
  RUN_TEST(test_user_on_after_off_notification_bypasses_dashboard_cadence);
  RUN_TEST(test_boot_animation_uses_its_own_cadence);
  RUN_TEST(test_off_panel_notification_sound_waits_for_presentation);
  RUN_TEST(test_notification_presentation_failures_retry_before_timer_or_sound);
  RUN_TEST(test_clear_overlay_queue_pending_and_visible_restores_snapshot);
  RUN_TEST(test_notification_queued_during_first_boot_waits_for_running);
  RUN_TEST(test_reaction_queued_during_boot_renders_clean_base);
  RUN_TEST(test_promoted_overlay_timer_starts_when_presented);
  RUN_TEST(test_overlay_queue_rejects_new_tail_when_full);
  RUN_TEST(test_notification_text_limit_bounds_retained_queue_memory);
  RUN_TEST(test_notification_fifo_preserves_fixed_payloads);
  RUN_TEST(test_mixed_overlay_fifo_freezes_reaction_base_and_never_sounds_it);
  RUN_TEST(test_reaction_duration_crosses_uint32_wrap_without_sound);
  RUN_TEST(test_clear_and_explicit_off_cancel_the_entire_overlay_queue);
  RUN_TEST(test_dashboard_selection_during_on_panel_notification_uses_live_base);
  RUN_TEST(test_start_rejects_invalid_initial_dashboard_safely);
  RUN_TEST(test_start_rejects_empty_resolved_dashboard_safely);
  RUN_TEST(test_restart_invalid_dashboard_clears_running_selection_safely);
  RUN_TEST(test_start_brightness_bounce_syncs_from_25_percent);
  RUN_TEST(test_external_brightness_syncs_bounce_from_25_percent);
  RUN_TEST(test_same_brightness_echo_preserves_bounce_direction);
  RUN_TEST(test_fifo_stops_head_sound_and_waits_for_next_presentation);
  RUN_TEST(test_pending_notification_cancellation_does_not_stop_unrelated_sound);
  RUN_TEST(test_presented_silent_notification_preserves_unrelated_sound_when_on);
  RUN_TEST(test_notification_expiry_stops_sound_before_off_restore);
  RUN_TEST(test_pending_off_notification_stops_boot_sound_before_power_restore);
  RUN_TEST(test_silent_off_notification_stops_boot_sound_before_restore);
  RUN_TEST(test_brightness_only_preserves_overlay_sound_and_timer);
  RUN_TEST(test_off_state_brightness_change_preserves_temporary_wake);
  RUN_TEST(test_power_commands_cancel_a_temporary_off_state_wake);
  RUN_TEST(test_logical_off_stops_sound_before_power_off);
  RUN_TEST(test_reboot_uses_typed_system_port);
  RUN_TEST(test_factory_reset_uses_typed_system_port);
  RUN_TEST(test_timing_minimize_preserves_session_and_dashboard_policy);
  RUN_TEST(test_timing_minimize_idle_and_idempotence);
  RUN_TEST(test_timing_minimize_off_wake_and_overlay_restoration);
  RUN_TEST(test_minimized_timer_completion_foreground_and_alarm);
  RUN_TEST(test_timing_minimize_button_contexts_and_live_overlays);
  RUN_TEST(test_timing_precision_pause_fresh_start_cap_and_reveal);
  RUN_TEST(test_timing_replacement_shared_controls_invalid_and_restart);
  RUN_TEST(test_timing_temporary_wake_user_override_and_off_wrap);
  RUN_TEST(test_timer_expiry_preempts_overlay_wakes_latches_and_bounds_alarm);
  RUN_TEST(test_timing_boot_suppression_and_overlay_restore);
  RUN_TEST(test_timing_brightness_gestures_capture_context);
  RUN_TEST(test_timing_wake_brightness_and_overlay_do_not_reinitialize);
  RUN_TEST(test_timing_cadence_microphone_and_overlay_dto);
  RUN_TEST(test_timer_alarm_full_repeat_window_and_matching_sound_owner);
  RUN_TEST(test_timing_button_overlay_context_and_no_closed_session_fallthrough);
  RUN_TEST(test_expired_timer_power_off_cancels_alarm_repeats);
  RUN_TEST(test_overdue_timer_stop_does_not_wake_or_announce_expiry);
  RUN_TEST(test_timer_hidden_expiry_wakes_without_sound_player);
  RUN_TEST(test_invalid_timer_start_preserves_overlay_and_finished_alarm);
  RUN_TEST(test_inbound_user_light_never_publishes_to_sink);
  return UNITY_END();
}
