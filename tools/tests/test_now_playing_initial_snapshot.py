"""Offline adapter reproduction: real extracted methods and content policy.

The fake API models missing entity/attribute responses and reconnect replay; it
is not ESPHome or Home Assistant. Artwork, position parsing, workers, persistence,
and networking are stubbed. Artwork readiness is injected into the real policy.
"""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
ADAPTER = ROOT / 'esphome/components/pixoo64'
CORE = ROOT / 'pixoo_content'


def unique(pattern, text):
    matches = list(re.finditer(pattern, text, re.MULTILINE | re.DOTALL))
    if len(matches) != 1:
        raise AssertionError(f'Expected one match for {pattern!r}, got {len(matches)}')
    return matches[0]


def method(text, name):
    match = unique(r'^[^\n]*HomeAssistantMediaSource::' + name + r'\s*\(', text)
    start = match.start()
    brace = text.find('{', match.end())
    if brace < 0:
        raise AssertionError(f'Missing body: {name}')
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    raise AssertionError(f'Unterminated body: {name}')


PREAMBLE = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "metadata_policy.h"
#include "now_playing_presentation.h"
using namespace pixoo::now_playing;
static uint32_t now_ms = 0;
static uint32_t millis() { return now_ms; }
using StringRef = std::string;
struct FakeAPI {
  struct Request { std::string attr; bool once; std::function<void(StringRef)> callback; };
  bool connected = true, exists = false;
  std::string root_state = "playing";
  std::map<std::string, std::string> attributes;
  std::vector<Request> requests;
  bool is_connected_with_state_subscription() { return connected; }
  void add(const char*, const char* attr, std::function<void(StringRef)> cb, bool once) {
    requests.push_back({attr ? attr : "", once, cb});
  }
  void get_home_assistant_state(const char* e, const char* a, std::function<void(StringRef)> cb) { add(e,a,cb,true); }
  void subscribe_home_assistant_state(const char* e, const char* a, std::function<void(StringRef)> cb) { add(e,a,cb,false); }
  void deliver(const Request &r) {
    if (!exists) return;
    if (r.attr.empty()) { r.callback(root_state); return; }
    auto i = attributes.find(r.attr);
    if (i != attributes.end()) r.callback(i->second);
  }
  void sweep() { for (const auto &r : requests) deliver(r); }
  void live() { for (const auto &r : requests) if (!r.once) deliver(r); }
  void root() { for (const auto &r : requests) if (!r.once && r.attr.empty()) deliver(r); }
};
struct FetchPolicy { void ResetRetry() {} };
struct HomeAssistantMediaSource {
  // The declarations below are copied from the production header by the test.
'''

HARNESS = r'''
  FakeAPI *api_server_ = nullptr;
  struct { char entity_id[64] = "media_player.synthetic"; } record_;
  FetchPolicy artwork_fetch_policy_;
  NowPlayingData published{};
  void RegisterSubscriptions_(); void OnApiClientConnected(const std::string &);
  void BeginInitialSnapshot_();
  void CompleteInitialSnapshot_(); void MarkInitialField_(InitialField);
  void SetTransportConnected_(bool, uint32_t); void ResetState_(uint32_t);
  void loop(); bool SnapshotSettled() const;
  void OnSubscriptionRoot_(StringRef); void OnRoot_(StringRef);
  void OnRootState_(PlaybackState, uint32_t);
  void OnContentId_(StringRef v) { policy_.OnContentId(v.c_str(), v.size(), millis()); }
  void OnTitle_(StringRef); void OnArtist_(StringRef);
  void OnDuration_(StringRef) {} void OnPosition_(StringRef) {}
  void OnPositionUpdatedAt_(StringRef) {}
  // URL resolution and image work are outside this fixture's boundary.
  void OnEntityPicture_(StringRef v) {
    policy_.OnArtworkIdentity(!v.empty(), v.empty() ? 0 : 42, millis());
  }
  void ClearPositionTracking_() {} void ClearDesiredArtwork_() {}
  void ObserveArtworkCompletion_(uint32_t) {} void ScheduleArtwork_(uint32_t) {}
  void Publish_(const NowPlayingData &d) { published = d; }
  void artwork_ready() {
    policy_.OnArtworkIdentity(true, 42, millis());
    NowPlayingData d;
    if (policy_.ForcePublish(millis(), &d)) Publish_(d);
    if (published.artwork_availability == ArtworkAvailability::kPending) {
      assert(policy_.CompleteArtwork(42, published.artwork_revision, true, millis(), &d));
      Publish_(d);
    }
  }
};
'''

TESTS = r'''
static void available(FakeAPI &api) {
  api.exists = true;
  api.attributes = {{"friendly_name", "Example"}, {"supported_features", "1"},
    {"media_content_id", "synthetic-track-1"}, {"media_title", "Title"},
    {"media_artist", "Artist"}, {"media_duration", "120"},
    {"media_position", "5"}, {"media_position_updated_at", ""},
    {"entity_picture", "/synthetic-art"}};
}
static void init(HomeAssistantMediaSource &s, FakeAPI &api) {
  now_ms = 0; s.api_server_ = &api; s.configured_ = true;
  s.transport_connected_ = true; s.ResetState_(0); s.RegisterSubscriptions_();
  assert(api.requests.size() == 10);
  assert(api.requests.front().attr == "friendly_name" && api.requests.front().once);
  assert(api.requests.back().attr == "supported_features" && api.requests.back().once);
  for (size_t i = 1; i < 9; ++i) assert(!api.requests[i].once);
}
static void ready_art(HomeAssistantMediaSource &s) {
  now_ms += 1000; s.loop(); s.artwork_ready();
  assert(s.published.source_state == NowPlayingSourceState::kReady);
  assert(s.published.artwork_availability == ArtworkAvailability::kReady);
}
static void gate(HomeAssistantMediaSource &s, bool expected) {
  assert(s.SnapshotSettled() == expected);
  assert(NowPlayingPresentation::ReadyToShow(s.published, s.SnapshotSettled()) == expected);
}
static void normal() {
  FakeAPI api; HomeAssistantMediaSource s; available(api); init(s,api);
  api.sweep(); assert(s.initial_snapshot_complete_);
  ready_art(s); gate(s,true);
  auto d = s.published;
  assert(!NowPlayingPresentation::ReadyToShow(d, false));
  d.artwork_availability = ArtworkAvailability::kPending;
  assert(!NowPlayingPresentation::ReadyToShow(d, true));
  d.artwork_availability = ArtworkAvailability::kFailed;
  assert(NowPlayingPresentation::ReadyToShow(d, true));
}
static void absent_then_reconnect() {
  FakeAPI api; HomeAssistantMediaSource s; init(s,api); api.sweep();
  now_ms = 6000; s.loop(); assert(!s.SnapshotSettled());
  available(api); api.live(); now_ms += kInitialSnapshotQuietMs; s.loop();
  ready_art(s); gate(s,true);
  assert(std::string(s.published.title.bytes) == "Title");
  api.connected = false; s.loop(); assert(!s.SnapshotSettled());
  api.connected = true; s.loop(); api.attributes.erase("media_artist");
  api.sweep(); ready_art(s); gate(s,true);
  assert(std::string(s.published.artist.bytes).empty());
  api.connected = false; s.loop();
  api.connected = true; s.loop();
  api.attributes.erase("media_title"); api.attributes.erase("entity_picture");
  api.attributes["media_content_id"] = "synthetic-track-2";
  api.sweep();
  assert(s.initial_snapshot_complete_);
  assert(s.published.title.size == 0 && s.published.artist.size == 0);
  assert(s.published.artwork_known && !s.published.has_artwork_identity);
  gate(s,true);
}
static void reconnect_with_other_subscriber() {
  FakeAPI api; HomeAssistantMediaSource s; available(api); init(s,api);
  api.sweep(); ready_art(s); gate(s,true);
  s.OnApiClientConnected("ESPHome Logs"); gate(s,true);
  // Aggregate subscriptions stay connected while HA authenticates a new session.
  assert(api.connected);
  s.OnApiClientConnected("Home Assistant 2026.11.1");
  assert(!s.SnapshotSettled() && !s.initial_root_seen_);
  api.attributes.erase("media_artist");
  api.attributes.erase("entity_picture");
  api.sweep();
  assert(s.initial_snapshot_complete_);
  assert(s.published.artist.size == 0 && !s.published.has_artwork_identity);
  gate(s,true);
  s.OnApiClientConnected("Home Assistant");
  api.exists = false; api.sweep(); s.loop();
  now_ms += kInitialSnapshotMaxDelayMs; s.loop();
  assert(!s.SnapshotSettled());
  available(api); api.live(); ready_art(s); gate(s,true);
}
static void missing(const char *attribute) {
  FakeAPI api; HomeAssistantMediaSource s; available(api);
  if (*attribute) api.attributes.erase(attribute);
  if (std::string(attribute) == "both") {
    api.attributes.erase("friendly_name"); api.attributes.erase("supported_features");
  }
  init(s,api); api.sweep();
  if (std::string(attribute) != "friendly_name") {
    now_ms = kInitialSnapshotQuietMs - 1; s.loop(); assert(!s.initial_snapshot_complete_);
    ++now_ms; s.loop();
  }
  assert(s.initial_snapshot_complete_);
  ready_art(s); gate(s,true);
  assert(std::string(s.published.title.bytes) == "Title");
  api.attributes["friendly_name"] = "Example";
  api.attributes["supported_features"] = "1";
  const uint32_t revision = s.published.publication_revision;
  api.deliver(api.requests.front()); api.deliver(api.requests.back());
  assert(s.initial_snapshot_complete_ && s.published.publication_revision == revision);
  gate(s,true);
}
static void attributes_before_root() {
  FakeAPI api; HomeAssistantMediaSource s; available(api); init(s,api);
  s.OnApiClientConnected("Home Assistant 2026.11.1");
  for (size_t i = 2; i < 9; ++i) api.deliver(api.requests[i]);
  api.root(); api.deliver(api.requests.front()); api.deliver(api.requests.back());
  assert(s.initial_snapshot_complete_);
  assert(std::string(s.published.title.bytes) == "Title");
  assert(std::string(s.published.artist.bytes) == "Artist");
  assert(s.published.artwork_availability == ArtworkAvailability::kPending);
  ready_art(s); gate(s,true);
}
static void artwork_gate() {
  FakeAPI api; HomeAssistantMediaSource s; available(api); init(s,api);
  api.live(); now_ms = kInitialSnapshotQuietMs; s.loop();
  assert(s.SnapshotSettled());
  assert(s.published.artwork_availability == ArtworkAvailability::kPending);
  assert(!NowPlayingPresentation::ReadyToShow(s.published, s.SnapshotSettled()));
  now_ms += kInitialSnapshotMaxDelayMs; s.loop();
  assert(!NowPlayingPresentation::ReadyToShow(s.published, s.SnapshotSettled()));
  NowPlayingData failed;
  assert(s.policy_.CompleteArtwork(42, s.published.artwork_revision, false, now_ms, &failed));
  s.Publish_(failed); gate(s,true);
}
static void late_picture() {
  FakeAPI api; HomeAssistantMediaSource s; available(api); init(s,api);
  api.attributes.erase("entity_picture"); api.live();
  now_ms = kInitialSnapshotQuietMs; s.loop(); gate(s,true);
  api.attributes["entity_picture"] = "/synthetic-art";
  api.deliver(api.requests[8]);
  now_ms += kInitialSnapshotQuietMs; s.loop();
  assert(s.SnapshotSettled());
  assert(s.published.artwork_availability == ArtworkAvailability::kPending);
  assert(!NowPlayingPresentation::ReadyToShow(s.published, s.SnapshotSettled()));
  ready_art(s); gate(s,true);
}
static void root_before_begin() {
  FakeAPI api; HomeAssistantMediaSource s; available(api); init(s,api);
  api.live(); api.deliver(api.requests.front()); api.deliver(api.requests.back());
  assert(s.initial_snapshot_complete_); ready_art(s); gate(s,true);
  assert(std::string(s.published.title.bytes) == "Title");
}
static void end_before_root() {
  FakeAPI api; HomeAssistantMediaSource s; available(api); init(s,api);
  api.deliver(api.requests.front()); api.deliver(api.requests.back());
  now_ms = 6000; s.loop(); assert(!s.initial_snapshot_complete_);
  api.root(); now_ms += kInitialSnapshotQuietMs; s.loop();
  assert(s.initial_snapshot_complete_);
}
static void no_root() {
  FakeAPI api; HomeAssistantMediaSource s; available(api); init(s,api);
  api.deliver(api.requests.front()); api.deliver(api.requests[2]);
  api.deliver(api.requests.back());
  now_ms = 60000; s.loop(); assert(!s.initial_snapshot_complete_);
  assert(!s.SnapshotSettled());
}
static void deadline() {
  FakeAPI api; HomeAssistantMediaSource s; available(api); init(s,api);
  api.root();
  for (now_ms = 100; now_ms < kInitialSnapshotMaxDelayMs; now_ms += 100) {
    api.deliver(api.requests[6]); s.loop(); assert(!s.initial_snapshot_complete_);
  }
  now_ms = kInitialSnapshotMaxDelayMs; api.deliver(api.requests[6]); s.loop();
  assert(s.initial_snapshot_complete_);
}
static void wrap() {
  FakeAPI api; HomeAssistantMediaSource s; available(api); init(s,api);
  now_ms = UINT32_MAX - 40; api.root();
  now_ms += kInitialSnapshotQuietMs - 1; s.loop(); assert(!s.initial_snapshot_complete_);
  ++now_ms; s.loop(); assert(s.initial_snapshot_complete_);
}
static void unavailable() {
  FakeAPI api; HomeAssistantMediaSource s; available(api); init(s,api);
  api.root_state = "unavailable"; api.sweep();
  api.root_state = "playing"; api.live(); ready_art(s); gate(s,true);
}
int main(int argc, char **argv) {
  assert(argc == 2); std::string name = argv[1];
  if (name == "normal") normal();
  else if (name == "absent") absent_then_reconnect();
  else if (name == "begin") missing("friendly_name");
  else if (name == "end") missing("supported_features");
  else if (name == "both") missing("both");
  else if (name == "late_begin") root_before_begin();
  else if (name == "early_end") end_before_root();
  else if (name == "no_root") no_root();
  else if (name == "deadline") deadline();
  else if (name == "wrap") wrap();
  else if (name == "unavailable") unavailable();
  else if (name == "artwork") artwork_gate();
  else if (name == "late_picture") late_picture();
  else if (name == "other_subscriber") reconnect_with_other_subscriber();
  else if (name == "attributes_first") attributes_before_root();
  else assert(false);
}
'''


@unittest.skipUnless(shutil.which('c++'), 'requires native C++ compiler')
class InitialSnapshotTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cpp = (ADAPTER / 'home_assistant_media_source.cpp').read_text()
        header = (ADAPTER / 'home_assistant_media_source.h').read_text()
        enum = unique(r'  enum InitialField : uint8_t \{.*?  static constexpr uint8_t kAllInitialFields = .*?;', header).group()
        names = ['policy_', 'configured_', 'subscriptions_registered_', 'transport_connected_',
                 'connected_grace_active_', 'initial_snapshot_started_', 'initial_snapshot_complete_',
                 'initial_root_seen_', 'initial_fields_seen_', 'initial_root_state_',
                 'initial_root_received_ms_', 'initial_last_update_ms_',
                 'connected_since_ms_', 'config_revision_', 'position_context_allowed_',
                 'raw_position_pending_content_id_', 'content_id_observed_', 'has_explicit_content_id_',
                 'last_content_id_size_', 'last_content_id_']
        fields = []
        for name in names:
            fields.append(unique(r'^  [^;\n]*\b' + name + r'\b[^;]*;', header).group())
        methods = ['RegisterSubscriptions_', 'OnApiClientConnected', 'BeginInitialSnapshot_',
                   'CompleteInitialSnapshot_',
                   'MarkInitialField_', 'SetTransportConnected_', 'ResetState_', 'loop',
                   'OnSubscriptionRoot_', 'OnRoot_', 'OnRootState_', 'SnapshotSettled',
                   'OnTitle_', 'OnArtist_']
        constants = '\n'.join(unique(r'^constexpr uint32_t ' + name + r'\s*=\s*[^;]+;', cpp).group()
                              for name in ('kInitialConnectedGraceMs', 'kInitialSnapshotQuietMs',
                                           'kInitialSnapshotMaxDelayMs'))
        source = PREAMBLE.replace('struct HomeAssistantMediaSource {',
                                  constants + '\nstruct HomeAssistantMediaSource {')
        source += enum + '\n' + '\n'.join(fields) + HARNESS
        source += '\n'.join(method(cpp, name) for name in methods) + TESTS
        cls.tmp = tempfile.TemporaryDirectory(prefix='pixoo-snapshot-')
        cls.addClassCleanup(cls.tmp.cleanup)
        fixture = pathlib.Path(cls.tmp.name) / 'fixture.cpp'
        cls.binary = pathlib.Path(cls.tmp.name) / 'fixture'
        fixture.write_text(source)
        src = CORE / 'src/dashboard/now_playing'
        subprocess.run(['c++', '-std=c++17', '-I', str(CORE / 'include/dashboard/now_playing'),
                        str(fixture), *(str(src / f) for f in [
                            'metadata_policy.cpp', 'now_playing_data.cpp',
                            'now_playing_presentation.cpp', 'now_playing_timing.cpp',
                            'now_playing_text.cpp', 'now_playing_art.cpp']),
                        '-o', str(cls.binary)], check=True)

    def scenario(self, name):
        subprocess.run([str(self.binary), name], check=True)

    def test_normal_fast(self): self.scenario('normal')
    def test_absent_then_reconnect_sparse(self): self.scenario('absent')
    def test_missing_begin(self): self.scenario('begin')
    def test_missing_end(self): self.scenario('end')
    def test_missing_both(self): self.scenario('both')
    def test_late_begin(self): self.scenario('late_begin')
    def test_end_before_root(self): self.scenario('early_end')
    def test_no_root_never_completes(self): self.scenario('no_root')
    def test_periodic_updates_deadline(self): self.scenario('deadline')
    def test_clock_wrap(self): self.scenario('wrap')
    def test_unavailable_then_active(self): self.scenario('unavailable')
    def test_pending_artwork_still_blocks(self): self.scenario('artwork')
    def test_late_picture_blocks_until_ready(self): self.scenario('late_picture')
    def test_reconnect_with_other_subscriber(self): self.scenario('other_subscriber')
    def test_attributes_before_root(self): self.scenario('attributes_first')


if __name__ == '__main__':
    unittest.main()
