#include <unity.h>
#include <cstring>
#include <initializer_list>
#include <type_traits>
#include "now_playing_presentation.h"

using namespace pixoo::now_playing;
using Model = NowPlayingPresentation;
using Kind = Model::TransitionKind;

// Tokens represent content equality, not pixels or images.
struct Artwork final : NowPlayingPresentationArtwork {
  uint64_t slots[2]{};
  uint64_t content{1};
  bool success{true};
  int copies{0};
  int placeholders{0};
  ArtworkAverage PreparePlaceholder(int8_t slot, uint64_t key) override {
    ++placeholders;
    slots[slot] = key;
    return {11, 22, 33};
  }
  bool CopyArtwork(int8_t slot, uint64_t, uint32_t, ArtworkAverage &average) override {
    ++copies;
    if (!success) return false;
    slots[slot] = content;
    average = {44, 55, 66};
    return true;
  }
  bool EqualArtwork(int8_t a, int8_t b) const override { return slots[a] == slots[b]; }
};

static NowPlayingData Data(uint64_t id = 1, const char *title = "A") {
  NowPlayingData d;
  d.source_state = NowPlayingSourceState::kReady;
  d.playback_state = PlaybackState::kPlaying;
  d.media_identity = id;
  d.artwork_known = true;
  d.has_artwork_identity = true;
  d.artwork_identity = id;
  d.artwork_revision = 1;
  d.artwork_availability = ArtworkAvailability::kReady;
  d.title.size = std::strlen(title);
  std::memcpy(d.title.bytes, title, d.title.size + 1);
  return d;
}
static NowPlayingData ArtistData(uint64_t id, const char *title, const char *artist) {
  auto d = Data(id, title);
  d.artist.size = std::strlen(artist);
  TEST_ASSERT_TRUE(d.artist.size < sizeof(d.artist.bytes));
  std::memcpy(d.artist.bytes, artist, d.artist.size + 1);
  return d;
}
static void Artist(const Model &m, const char *artist) {
  TEST_ASSERT_EQUAL_STRING(artist, m.Displayed().artist.bytes);
}
static void FrontArtwork(const Model &m, int front, uint64_t id, uint32_t revision) {
  TEST_ASSERT_EQUAL_INT(front, m.FrontIndex());
  TEST_ASSERT_EQUAL_UINT64(id, m.Front().artwork_identity);
  TEST_ASSERT_EQUAL_UINT32(revision, m.Front().artwork_revision);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Model::BufferKind::kArtwork),
                        static_cast<int>(m.Front().kind));
}
static void IsKind(const Model &m, Kind k) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(k), static_cast<int>(m.Kind()));
}
static void Title(const Model &m, const char *title) {
  TEST_ASSERT_EQUAL_STRING(title, m.Displayed().title.bytes);
}
static void test_presentation_direct_ready_and_pending() {
  Model m; Artwork a; auto d = ArtistData(1, "A", "Artist A");
  m.Update(d, 0, a);
  TEST_ASSERT_TRUE(m.Initialized());
  TEST_ASSERT_EQUAL_INT(1, a.copies);
  TEST_ASSERT_EQUAL_INT(1, a.placeholders);
  TEST_ASSERT_EQUAL_UINT8(44, m.Front().average_red);
  TEST_ASSERT_EQUAL_UINT8(55, m.Front().average_green);
  TEST_ASSERT_EQUAL_UINT8(66, m.Front().average_blue);
  const auto front = m.FrontIndex();
  d = ArtistData(2, "B", "Artist B"); d.artwork_availability = ArtworkAvailability::kPending;
  m.Update(d, 5000, a);
  IsKind(m, Kind::kNone); Title(m, "A"); Artist(m, "Artist A");
  FrontArtwork(m, front, 1, 1);
  TEST_ASSERT_EQUAL_INT(front, m.FrontIndex());
  TEST_ASSERT_EQUAL_UINT64(1, m.Front().artwork_identity);
  TEST_ASSERT_EQUAL_INT(1, a.copies);
}
static void test_presentation_artwork_midpoint_and_commit() {
  Model m; Artwork a; m.Update(Data(), 0, a);
  a.content = 2; auto d = Data(2, "B");
  m.Update(d, 100, a); IsKind(m, Kind::kArtwork);
  Title(m, "A"); TEST_ASSERT_EQUAL_UINT8(0, m.Crossfade());
  m.Update(d, 450, a); Title(m, "A");
  TEST_ASSERT_EQUAL_UINT8(0, m.TextOpacity());
  m.Update(d, 451, a); Title(m, "A");
  TEST_ASSERT_EQUAL_UINT8(0, m.TextOpacity());
  m.Update(d, 452, a); Title(m, "B");
  TEST_ASSERT_EQUAL_UINT8(0, m.TextOpacity());
  TEST_ASSERT_UINT8_WITHIN(2, 128, m.Crossfade());
  TEST_ASSERT_EQUAL_UINT64(1, m.Front().artwork_identity);
  m.Update(d, 625, a); TEST_ASSERT_TRUE(m.TextOpacity() > 100);
  m.Update(d, 800, a); IsKind(m, Kind::kNone); Title(m, "B");
  TEST_ASSERT_EQUAL_UINT64(2, m.Front().artwork_identity);
  TEST_ASSERT_EQUAL_UINT8(255, m.TextOpacity());
  TEST_ASSERT_EQUAL_INT(2, a.copies);
}
static void test_presentation_metadata_only() {
  Model m; Artwork a; m.Update(Data(), 0, a);
  auto d = Data(1, "B"); const int front = m.FrontIndex();
  m.Update(d, 100, a); IsKind(m, Kind::kText);
  m.Update(d, 452, a); Title(m, "B");
  m.Update(d, 800, a); IsKind(m, Kind::kNone);
  TEST_ASSERT_EQUAL_INT(front, m.FrontIndex());
  TEST_ASSERT_EQUAL_INT(1, a.copies);
}
static void test_presentation_revision_and_stable_media() {
  Model m; Artwork a; auto d = ArtistData(1, "A", "Artist A"); m.Update(d, 0, a);
  ++d.artwork_revision; a.content = 9;
  m.Update(d, 10, a); IsKind(m, Kind::kArtwork);
  m.Update(d, 710, a);
  TEST_ASSERT_EQUAL_UINT32(2, m.Front().artwork_revision);
  const int front = m.FrontIndex(); const int copies = a.copies;
  d = ArtistData(1, "B", "Artist B");
  d.artwork_revision = 2; d.artwork_identity = 8; a.content = 8;
  d.artwork_availability = ArtworkAvailability::kPending;
  m.Update(d, 715, a); IsKind(m, Kind::kNone);
  FrontArtwork(m, front, 1, 2); Title(m, "A"); Artist(m, "Artist A");
  TEST_ASSERT_EQUAL_INT(copies, a.copies);
  d.artwork_availability = ArtworkAvailability::kReady;
  m.Update(d, 720, a); IsKind(m, Kind::kArtwork);
  Title(m, "A"); Artist(m, "Artist A");
  m.Update(d, 1420, a); IsKind(m, Kind::kNone);
  FrontArtwork(m, 1 - front, 8, 2); Title(m, "B"); Artist(m, "Artist B");
  TEST_ASSERT_EQUAL_UINT64(1, m.Displayed().media_identity);
  TEST_ASSERT_EQUAL_UINT64(8, m.Front().artwork_identity);
}
static void test_presentation_equal_content_adopts_identity() {
  Model m; Artwork a; m.Update(ArtistData(1, "A", "Artist A"), 0, a);
  const int front = m.FrontIndex(); auto d = ArtistData(2, "B", "Artist B");
  d.media_identity = 1; d.artwork_availability = ArtworkAvailability::kPending;
  m.Update(d, 50, a); IsKind(m, Kind::kNone);
  Title(m, "A"); Artist(m, "Artist A"); FrontArtwork(m, front, 1, 1);
  TEST_ASSERT_EQUAL_INT(1, a.copies);
  d.artwork_availability = ArtworkAvailability::kReady;
  m.Update(d, 100, a); IsKind(m, Kind::kText);
  Artist(m, "Artist A");
  TEST_ASSERT_EQUAL_UINT64(2, m.Front().artwork_identity);
  TEST_ASSERT_EQUAL_INT(front, m.FrontIndex());
  TEST_ASSERT_EQUAL_UINT8(0, m.Crossfade()); Title(m, "A");
  m.Update(d, 451, a); Title(m, "A"); Artist(m, "Artist A");
  m.Update(d, 452, a); Title(m, "B"); Artist(m, "Artist B");
  FrontArtwork(m, front, 2, 1); IsKind(m, Kind::kText);
  m.Update(d, 800, a); Title(m, "B"); Artist(m, "Artist B");
  d = ArtistData(3, "B", "Artist B"); d.media_identity = 1; m.Update(d, 900, a);
  IsKind(m, Kind::kNone); TEST_ASSERT_EQUAL_UINT64(3, m.Front().artwork_identity);
  FrontArtwork(m, front, 3, 1); Title(m, "B"); Artist(m, "Artist B");
}
static void test_presentation_copy_failure_and_recovery() {
  Model m; Artwork a; a.success = false;
  m.Update(Data(), 0, a); IsKind(m, Kind::kNone);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Model::BufferKind::kPlaceholder), static_cast<int>(m.Front().kind));
  m.Update(Data(), 10, a); // Same placeholder content: failed copy does not fade.
  IsKind(m, Kind::kNone);
  TEST_ASSERT_EQUAL_INT(2, a.copies);
  a.success = true; m.Update(Data(), 20, a); IsKind(m, Kind::kArtwork);
  m.Update(Data(), 720, a);
  TEST_ASSERT_EQUAL_UINT64(1, m.Front().artwork_identity);
  a.success = false; auto d = Data(2, "B");
  m.Update(d, 800, a); IsKind(m, Kind::kIdentityPlaceholder);
  m.Update(d, 1400, a); Title(m, "B");
  TEST_ASSERT_EQUAL_UINT64(0, m.Front().artwork_identity);
}
static void test_presentation_placeholder_to_ready() {
  Model m; Artwork a; auto d = Data();
  d.artwork_availability = ArtworkAvailability::kFailed;
  m.Update(d, 0, a); TEST_ASSERT_EQUAL_INT(0, a.copies);
  d.artwork_availability = ArtworkAvailability::kReady;
  m.Update(d, 50, a); IsKind(m, Kind::kArtwork);
  m.Update(d, 750, a); TEST_ASSERT_EQUAL_UINT64(1, m.Front().artwork_identity);
}
static void test_presentation_interrupt_restores_coherence() {
  Model m; Artwork a; m.Update(ArtistData(1, "A", "Artist A"), 0, a);
  const int front = m.FrontIndex(); a.content = 2;
  auto b = ArtistData(2, "B", "Artist B"); m.Update(b, 100, a);
  Title(m, "A"); Artist(m, "Artist A"); FrontArtwork(m, front, 1, 1);
  TEST_ASSERT_EQUAL_INT(1 - front, m.TargetIndex());
  TEST_ASSERT_EQUAL_UINT64(2, m.Target().artwork_identity);
  TEST_ASSERT_EQUAL_UINT32(1, m.Target().artwork_revision);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Model::BufferKind::kArtwork), static_cast<int>(m.Target().kind));
  m.Update(b, 451, a); Title(m, "A"); Artist(m, "Artist A");
  FrontArtwork(m, front, 1, 1);
  m.Update(b, 452, a); Title(m, "B"); Artist(m, "Artist B");
  FrontArtwork(m, front, 1, 1);
  auto c = ArtistData(3, "C", "Artist C"); c.artwork_availability = ArtworkAvailability::kPending;
  const int copies = a.copies;
  m.Update(c, 5000, a); // Must cancel before B can commit on this delayed tick.
  IsKind(m, Kind::kNone); Title(m, "A"); Artist(m, "Artist A");
  FrontArtwork(m, front, 1, 1); TEST_ASSERT_EQUAL_INT(copies, a.copies);
  TEST_ASSERT_EQUAL_INT(front, m.FrontIndex());
  TEST_ASSERT_EQUAL_UINT64(1, m.Front().artwork_identity);
  c.artwork_availability = ArtworkAvailability::kFailed;
  m.Update(c, 5010, a); IsKind(m, Kind::kIdentityPlaceholder);
  Title(m, "A"); Artist(m, "Artist A"); FrontArtwork(m, front, 1, 1);
  TEST_ASSERT_EQUAL_INT(1 - front, m.TargetIndex());
  TEST_ASSERT_EQUAL_UINT64(0, m.Target().artwork_identity);
  TEST_ASSERT_EQUAL_UINT32(0, m.Target().artwork_revision);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Model::BufferKind::kPlaceholder), static_cast<int>(m.Target().kind));
  m.Update(c, 5311, a); Title(m, "A"); Artist(m, "Artist A");
  FrontArtwork(m, front, 1, 1);
  m.Update(c, 5312, a); Title(m, "C"); Artist(m, "Artist C");
  FrontArtwork(m, front, 1, 1);
  auto idle = Data(); idle.playback_state = PlaybackState::kIdle;
  idle.has_artwork_identity = false; idle.title = {};
  m.Update(idle, 9000, a); Title(m, "A"); IsKind(m, Kind::kIdentityPlaceholder);
  Artist(m, "Artist A"); FrontArtwork(m, front, 1, 1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Model::BufferKind::kPlaceholder), static_cast<int>(m.Target().kind));
  TEST_ASSERT_EQUAL_UINT64(0, m.Target().artwork_identity);
  a.content = 4; auto d = ArtistData(4, "D", "Artist D");
  m.Update(d, 10000, a); Title(m, "A"); IsKind(m, Kind::kArtwork);
  Artist(m, "Artist A"); FrontArtwork(m, front, 1, 1);
  TEST_ASSERT_EQUAL_INT(1 - front, m.TargetIndex());
  TEST_ASSERT_EQUAL_UINT64(4, m.Target().artwork_identity);
  TEST_ASSERT_EQUAL_UINT32(1, m.Target().artwork_revision);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Model::BufferKind::kArtwork), static_cast<int>(m.Target().kind));
  m.Update(d, 10351, a); Title(m, "A"); Artist(m, "Artist A");
  FrontArtwork(m, front, 1, 1);
  m.Update(d, 10352, a); Title(m, "D"); Artist(m, "Artist D");
  FrontArtwork(m, front, 1, 1);
  m.Update(d, 10700, a); Title(m, "D"); Artist(m, "Artist D");
  IsKind(m, Kind::kNone); FrontArtwork(m, 1 - front, 4, 1);
  TEST_ASSERT_EQUAL_UINT64(4, m.Front().artwork_identity);
}
static void test_presentation_artist_only_keeps_cover_and_title() {
  Model m; Artwork a; auto d = ArtistData(1, "A", "Artist A");
  m.Update(d, 0, a); const int front = m.FrontIndex();
  d = ArtistData(1, "A", "Artist B");
  m.Update(d, 100, a); IsKind(m, Kind::kText);
  Title(m, "A"); Artist(m, "Artist A"); FrontArtwork(m, front, 1, 1);
  m.Update(d, 451, a); Title(m, "A"); Artist(m, "Artist A");
  TEST_ASSERT_EQUAL_UINT8(0, m.TextOpacity());
  m.Update(d, 452, a); Title(m, "A"); Artist(m, "Artist B");
  TEST_ASSERT_EQUAL_UINT8(0, m.TextOpacity()); FrontArtwork(m, front, 1, 1);
  m.Update(d, 800, a); IsKind(m, Kind::kNone);
  Title(m, "A"); Artist(m, "Artist B"); FrontArtwork(m, front, 1, 1);
  TEST_ASSERT_EQUAL_UINT8(255, m.TextOpacity());
  TEST_ASSERT_EQUAL_INT(1, a.copies); TEST_ASSERT_EQUAL_INT(1, a.placeholders);
}
static void test_presentation_dim_retarget() {
  Model m; Artwork a; auto d = Data(); m.Update(d, 0, a);
  d.playback_state = PlaybackState::kPaused; m.Update(d, 100, a);
  TEST_ASSERT_EQUAL_UINT8(255, m.Dim());
  m.Update(d, 300, a); TEST_ASSERT_UINT8_WITHIN(1, 202, m.Dim());
  d.source_state = NowPlayingSourceState::kStale; m.Update(d, 300, a);
  m.Update(d, 700, a); TEST_ASSERT_EQUAL_UINT8(112, m.Dim());
  d.source_state = NowPlayingSourceState::kReady; m.Update(d, 710, a);
  m.Update(d, 1110, a); TEST_ASSERT_EQUAL_UINT8(150, m.Dim());
  d.playback_state = PlaybackState::kPlaying; m.Update(d, 1120, a);
  m.Update(d, 1520, a); TEST_ASSERT_EQUAL_UINT8(255, m.Dim());
}
static void test_presentation_dim_follows_displayed() {
  Model m; Artwork a; m.Update(Data(), 0, a);
  auto d = Data(2, "B"); d.source_state = NowPlayingSourceState::kStale;
  a.content = 2; m.Update(d, 100, a);
  m.Update(d, 450, a); TEST_ASSERT_EQUAL_UINT8(255, m.Dim());
  m.Update(d, 452, a); TEST_ASSERT_EQUAL_UINT8(255, m.Dim());
  m.Update(d, 652, a); TEST_ASSERT_UINT8_WITHIN(1, 183, m.Dim());
  m.Update(d, 852, a); TEST_ASSERT_EQUAL_UINT8(112, m.Dim());
}
static void test_presentation_marquee_widths_and_independence() {
  Model m; Artwork a; auto d = Data(); m.Update(d, 0, a);
  m.UpdateTextLayout(62, 60, 0); m.UpdateTextLayout(62, 60, 980);
  TEST_ASSERT_EQUAL_INT(1, m.TitleOffset()); TEST_ASSERT_EQUAL_INT(0, m.ArtistOffset());
  m.UpdateTextLayout(62, 60, 1000); m.UpdateTextLayout(62, 60, 1980);
  TEST_ASSERT_TRUE(m.TitleOffset() > m.ArtistOffset());
  TEST_ASSERT_EQUAL_INT(0, m.ArtistOffset());
  d.artist.size = 1; d.artist.bytes[0] = 'Z';
  m.Update(d, 2000, a); m.Update(d, 2700, a);
  m.UpdateTextLayout(62, 62, 2700);
  TEST_ASSERT_TRUE(m.TitleOffset() > 0); TEST_ASSERT_EQUAL_INT(0, m.ArtistOffset());
  m.UpdateTextLayout(62, 62, 3680);
  TEST_ASSERT_EQUAL_INT(1, m.ArtistOffset());
  d.media_identity = 99; m.Update(d, 3800, a); m.UpdateTextLayout(62, 62, 3800);
  TEST_ASSERT_EQUAL_INT(0, m.TitleOffset()); TEST_ASSERT_EQUAL_INT(0, m.ArtistOffset());
  m.UpdateTextLayout(62, 62, 1000000000);
  TEST_ASSERT_TRUE(m.TitleOffset() >= 0 && m.TitleOffset() < 74);
  m.UpdateTextLayout(60, 60, 1000000100);
  TEST_ASSERT_EQUAL_INT(0, m.TitleOffset()); TEST_ASSERT_EQUAL_INT(0, m.ArtistOffset());
}
static void test_presentation_wraparound() {
  Model m; Artwork a; uint32_t start = UINT32_MAX - 200;
  m.Update(Data(), start - 1, a); a.content = 2; auto d = Data(2, "B");
  m.Update(d, start, a); m.Update(d, start + 352, a); Title(m, "B");
  m.Update(d, start + 700, a); IsKind(m, Kind::kNone);
  m.UpdateTextLayout(62, 60, UINT32_MAX - 500);
  m.UpdateTextLayout(62, 60, 479);
  TEST_ASSERT_EQUAL_INT(1, m.TitleOffset());
  TEST_ASSERT_EQUAL_INT(0, m.ArtistOffset());
  d.playback_state = PlaybackState::kPaused;
  m.Update(d, UINT32_MAX - 100, a); m.Update(d, 299, a);
  TEST_ASSERT_EQUAL_UINT8(150, m.Dim());
}
static void test_presentation_readiness_matrix() {
  auto d = Data();
  TEST_ASSERT_TRUE(Model::ReadyToShow(d, true));
  TEST_ASSERT_FALSE(Model::ReadyToShow(d, false));
  for (auto state : {NowPlayingSourceState::kWaiting, NowPlayingSourceState::kStale,
                     NowPlayingSourceState::kNoEntityData, NowPlayingSourceState::kOffline}) {
    d.source_state = state; TEST_ASSERT_FALSE(Model::ReadyToShow(d, true));
  }
  d.source_state = NowPlayingSourceState::kUnconfigured;
  TEST_ASSERT_TRUE(Model::ReadyToShow(d, true));
  TEST_ASSERT_FALSE(Model::ReadyToShow(d, false));
  d = Data(); d.artwork_known = false; TEST_ASSERT_FALSE(Model::ReadyToShow(d, true));
  d.artwork_known = true;
  for (auto availability : {ArtworkAvailability::kPending, ArtworkAvailability::kNone}) {
    d.artwork_availability = availability; TEST_ASSERT_FALSE(Model::ReadyToShow(d, true));
  }
  d.artwork_availability = ArtworkAvailability::kFailed; TEST_ASSERT_TRUE(Model::ReadyToShow(d, true));
  d.has_artwork_identity = false; d.artwork_availability = ArtworkAvailability::kNone;
  TEST_ASSERT_TRUE(Model::ReadyToShow(d, true));
  d.playback_state = PlaybackState::kIdle; d.artwork_known = false;
  TEST_ASSERT_TRUE(Model::ReadyToShow(d, true));
}
static void test_presentation_hide_reentry() {
  Model m; Artwork a; m.OnShow(); m.Update(Data(), 0, a);
  a.content = 2; auto b = Data(2, "B"); m.Update(b, 100, a);
  m.OnShow(); m.Update(b, 452, a); Title(m, "B"); IsKind(m, Kind::kArtwork);
  m.OnHide(); m.OnShow(); TEST_ASSERT_FALSE(m.Initialized());
  a.content = 3; m.Update(Data(3, "C"), 500, a);
  Title(m, "C"); IsKind(m, Kind::kNone);
  TEST_ASSERT_EQUAL_UINT64(3, m.Front().artwork_identity);
  TEST_ASSERT_EQUAL_INT(3, a.copies);
  TEST_ASSERT_EQUAL_INT(2, a.placeholders);
}
static void test_presentation_bounded_value_state() {
  static_assert(std::is_trivially_destructible<Model>::value, "model owns only value state");
  static_assert(!std::is_polymorphic<Model>::value, "model has no persistent vptr");
  TEST_ASSERT_TRUE(sizeof(Model) < 2200);
}
void RunNowPlayingPresentationTests() {
  RUN_TEST(test_presentation_direct_ready_and_pending);
  RUN_TEST(test_presentation_artwork_midpoint_and_commit);
  RUN_TEST(test_presentation_metadata_only);
  RUN_TEST(test_presentation_revision_and_stable_media);
  RUN_TEST(test_presentation_equal_content_adopts_identity);
  RUN_TEST(test_presentation_copy_failure_and_recovery);
  RUN_TEST(test_presentation_placeholder_to_ready);
  RUN_TEST(test_presentation_interrupt_restores_coherence);
  RUN_TEST(test_presentation_artist_only_keeps_cover_and_title);
  RUN_TEST(test_presentation_dim_retarget);
  RUN_TEST(test_presentation_dim_follows_displayed);
  RUN_TEST(test_presentation_marquee_widths_and_independence);
  RUN_TEST(test_presentation_wraparound);
  RUN_TEST(test_presentation_readiness_matrix);
  RUN_TEST(test_presentation_hide_reentry);
  RUN_TEST(test_presentation_bounded_value_state);
}
