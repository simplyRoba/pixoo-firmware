#include "open_meteo.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

#include "async_worker.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/number/number.h"
#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "snapshot_buffer.h"
#include "open_meteo_url.h"
#include "refresh_policy.h"
#include "wmo.h"

using esphome::network::is_connected;

namespace esphome::pixoo64::adapters {

namespace {
const char *const TAG = "pixoo64.open_meteo";

// Cap on the response body we buffer. Current + a 12-hour hourly series (five
// fields) + daily fits well under this; the request bounds the series length.
constexpr size_t kMaxResponse = 16384;
}  // namespace

// Background worker state is kept out of the header so FreeRTOS does not leak
// into the host render-test build. The worker only reads a request snapshot and
// reports one completion; refresh policy and visible snapshots are main-task
// state.
struct OpenMeteoSource::Impl {
  struct Request {
    uint32_t generation{0};
    float latitude{0.0f};
    float longitude{0.0f};
  };
  struct Completion {
    Request request;
    pixoo::WeatherData data;
    bool succeeded{false};
  };
  struct TaggedWeatherData {
    pixoo::WeatherData data;
    uint32_t generation{0};
  };

  explicit Impl(std::function<void()> job) : worker(std::move(job)) {}
  async::AsyncWorker worker;
  pixoo::WeatherRefreshPolicy refresh;
  async::SnapshotBuffer<TaggedWeatherData> result;
  async::SnapshotBuffer<Completion> completion;
  // A release-store publishes the worker's completion buffer to the render
  // task. Only one request can be active, so the buffer cannot be overwritten
  // before the render task consumes it.
  std::atomic<bool> completion_ready{false};
  // The render task snapshots persisted entity values before waking the worker.
  std::atomic<uint32_t> request_generation{0};
  std::atomic<float> request_latitude{0.0f};
  std::atomic<float> request_longitude{0.0f};
};

OpenMeteoSource::OpenMeteoSource() {
  this->impl_ = new Impl([this]() { this->FetchJob_(); });
}

OpenMeteoSource::~OpenMeteoSource() {
  if (this->impl_ != nullptr) {
    this->impl_->worker.Stop();
    delete this->impl_;
    this->impl_ = nullptr;
  }
}

void OpenMeteoSource::setup() {
  if (!this->impl_->worker.Start())
    ESP_LOGE(TAG, "failed to start weather worker");
}

void OpenMeteoSource::on_shutdown() {
  if (this->impl_ != nullptr)
    this->impl_->worker.RequestStop();
}

bool OpenMeteoSource::teardown() {
  return this->impl_ == nullptr || this->impl_->worker.IsStopped();
}

bool OpenMeteoSource::HasData() const {
  if (this->impl_ == nullptr || this->impl_->worker.StopRequested() ||
      !this->impl_->refresh.HasCurrentData() ||
      !this->impl_->result.has_value())
    return false;
  return this->impl_->result.Get().generation ==
         this->impl_->refresh.location_generation();
}

pixoo::WeatherData OpenMeteoSource::Data() const {
  if (!this->HasData())
    return {};
  return this->impl_->result.Get().data;
}

void OpenMeteoSource::ObserveLocation_(float latitude, float longitude) {
  if (!this->location_observed_) {
    this->location_observed_ = true;
  } else if (latitude != this->observed_latitude_ ||
             longitude != this->observed_longitude_) {
    this->impl_->refresh.Invalidate();
  }
  this->observed_latitude_ = latitude;
  this->observed_longitude_ = longitude;
}

void OpenMeteoSource::FetchJob_() {
  if (this->impl_->worker.StopRequested())
    return;

  Impl::Request request;
  request.generation =
      this->impl_->request_generation.load(std::memory_order_acquire);
  request.latitude =
      this->impl_->request_latitude.load(std::memory_order_acquire);
  request.longitude =
      this->impl_->request_longitude.load(std::memory_order_acquire);
  if (this->impl_->worker.StopRequested())
    return;

  Impl::Completion completion;
  completion.request = request;
  completion.succeeded =
      this->fetch_(&completion.data, request.latitude, request.longitude);
  if (this->impl_->worker.StopRequested())
    return;
  // Stop may race this final checkpoint. The handoff remains in generated
  // static-lifetime state, and RequestRefresh() refuses to consume it after a
  // stop request.
  this->impl_->completion.Publish(completion);
  this->impl_->completion_ready.store(true, std::memory_order_release);
}

void OpenMeteoSource::ConsumeCompletion_(uint32_t now) {
  if (this->impl_->worker.StopRequested() ||
      !this->impl_->completion_ready.exchange(false,
                                               std::memory_order_acq_rel))
    return;

  const Impl::Completion completion = this->impl_->completion.Get();
  if (this->impl_->worker.StopRequested())
    return;
  if (this->impl_->refresh.Complete(completion.request.generation,
                                    completion.succeeded, now) !=
      pixoo::WeatherRefreshPolicy::Completion::kSuccess)
    return;

  // Tag the visible snapshot. HasData() hides it if the location generation
  // later changes, even while a refresh is pending or repeatedly failing.
  this->impl_->result.Publish(
      {completion.data, completion.request.generation});
}

uint32_t OpenMeteoSource::refresh_interval_ms_() const {
  if (this->refresh_interval_ == nullptr)
    return 1800000;  // 30 min
  const float minutes = this->refresh_interval_->state;
  if (!(minutes > 0.0f))
    return 1800000;
  return (uint32_t) (minutes * 60000.0f);
}

// Called by the dashboard while weather is shown. Signals the background worker
// to fetch only when the network is up, no fetch is already running, and the
// data is stale (older than refresh_interval_, or never fetched); a hidden
// dashboard never triggers a fetch. Returns immediately — the fetch runs off
// the render loop.
void OpenMeteoSource::RequestRefresh() {
  if (this->impl_ == nullptr || this->impl_->worker.StopRequested())
    return;

  const uint32_t now = millis();
  const float latitude = this->latitude_ != nullptr ? this->latitude_->state : 0.0f;
  const float longitude =
      this->longitude_ != nullptr ? this->longitude_->state : 0.0f;
  this->ObserveLocation_(latitude, longitude);
  if (this->impl_->worker.StopRequested())
    return;
  this->ConsumeCompletion_(now);
  if (!is_connected())
    return;

  uint32_t generation;
  if (!this->impl_->refresh.BeginRequest(now, this->refresh_interval_ms_(),
                                         &generation))
    return;
  this->impl_->request_generation.store(generation, std::memory_order_release);
  this->impl_->request_latitude.store(latitude, std::memory_order_release);
  this->impl_->request_longitude.store(longitude, std::memory_order_release);
  if (this->impl_->worker.Wake())
    return;
  // Retry task creation if setup ran while resources were temporarily
  // unavailable. Do not leave the policy with a request that cannot run.
  if (this->impl_->worker.Start() && this->impl_->worker.Wake())
    return;
  ESP_LOGE(TAG, "failed to start weather worker");
  this->impl_->refresh.Complete(generation, false, now);
}

bool OpenMeteoSource::fetch_(pixoo::WeatherData *out, float latitude,
                             float longitude) {
  if (this->http_ == nullptr || this->impl_->worker.StopRequested())
    return false;

  const std::string url = pixoo::BuildOpenMeteoForecastUrl(latitude, longitude);
  auto container = this->http_->get(url);
  if (container == nullptr) {
    ESP_LOGW(TAG, "fetch failed (status -1)");
    return false;
  }
  const auto end_container = [&container]() { container->end(); };
  if (this->impl_->worker.StopRequested()) {
    end_container();
    return false;
  }
  if (container->status_code != 200) {
    ESP_LOGW(TAG, "fetch failed (status %d)", container->status_code);
    end_container();
    return false;
  }

  // The API sends a chunked response, so content_length is 0 and cannot size
  // the read. Each read is subject to the configured HTTP timeout; the loop
  // also applies an elapsed deadline to the complete response body.
  std::vector<uint8_t> body;
  body.reserve(4096);
  uint8_t chunk[512];
  const uint32_t response_started = millis();
  const uint32_t response_timeout = this->http_->get_timeout();
  uint32_t last_data = response_started;
  bool complete = false;
  while (true) {
    if (this->impl_->worker.StopRequested()) {
      end_container();
      return false;
    }
    if (millis() - response_started >= response_timeout) {
      ESP_LOGW(TAG, "response body deadline exceeded (%zu bytes)", body.size());
      break;
    }

    const size_t remaining = kMaxResponse - body.size();
    uint8_t overflow_byte;
    const bool at_limit = remaining == 0;
    const int n = at_limit
                      ? container->read(&overflow_byte, 1)
                      : container->read(
                            chunk, std::min(sizeof(chunk), remaining));
    if (this->impl_->worker.StopRequested()) {
      end_container();
      return false;
    }
    if (millis() - response_started >= response_timeout) {
      ESP_LOGW(TAG, "response body deadline exceeded (%zu bytes)", body.size());
      break;
    }
    const auto step = http_request::http_read_loop_result(
        n, last_data, response_timeout, container->is_read_complete());
    if (step == http_request::HttpReadLoopResult::DATA) {
      if (at_limit) {
        ESP_LOGW(TAG, "response exceeds %zu-byte limit", kMaxResponse);
        break;
      }
      body.insert(body.end(), chunk, chunk + n);
    } else if (step == http_request::HttpReadLoopResult::COMPLETE) {
      complete = true;
      break;
    } else if (step == http_request::HttpReadLoopResult::RETRY) {
      continue;
    } else {  // ERROR or TIMEOUT
      break;
    }
  }
  end_container();
  if (!complete) {
    ESP_LOGW(TAG, "incomplete response (%zu bytes)", body.size());
    return false;
  }
  if (this->impl_->worker.StopRequested())
    return false;

  pixoo::WeatherData data{};
  data.has_location = true;
  data.latitude = latitude;
  data.longitude = longitude;
  const bool ok = json::parse_json(
      body.data(), body.size(), [&data](JsonObject root) -> bool {
        JsonObject cur = root["current"];
        if (cur.isNull())
          return false;
        data.is_night = (cur["is_day"] | 1) == 0;
        data.condition = pixoo::WmoToCondition(cur["weather_code"] | -1);
        if (cur["temperature_2m"].is<float>()) {
          data.has_temperature = true;
          data.temperature = cur["temperature_2m"];
        }
        if (cur["apparent_temperature"].is<float>()) {
          data.has_feels_like = true;
          data.feels_like = cur["apparent_temperature"];
        }
        if (cur["relative_humidity_2m"].is<float>()) {
          data.has_humidity = true;
          data.humidity = cur["relative_humidity_2m"];
        }

        JsonObject daily = root["daily"];
        if (!daily.isNull()) {
          JsonArray hi = daily["temperature_2m_max"];
          JsonArray lo = daily["temperature_2m_min"];
          if (!hi.isNull() && hi.size() > 0) {
            data.has_high = true;
            data.high = hi[0];
          }
          if (!lo.isNull() && lo.size() > 0) {
            data.has_low = true;
            data.low = lo[0];
          }
        }

        JsonObject hourly = root["hourly"];
        if (!hourly.isNull()) {
          JsonArray times = hourly["time"];
          JsonArray temps = hourly["temperature_2m"];
          JsonArray codes = hourly["weather_code"];
          JsonArray feels = hourly["apparent_temperature"];
          JsonArray hums = hourly["relative_humidity_2m"];
          JsonArray days = hourly["is_day"];
          // Buffer the whole series (starting at the fetch hour); which hours to
          // show is selected from the wall clock at render time.
          int count = 0;
          for (int i = 0; i < pixoo::kMaxHourSamples; i++) {
            if (temps.isNull() || (int) temps.size() <= i)
              break;
            pixoo::WeatherHourData &h = data.hours[i];
            h.valid = true;
            h.temperature = temps[i];
            if (!feels.isNull() && (int) feels.size() > i)
              h.feels_like = feels[i];
            if (!hums.isNull() && (int) hums.size() > i)
              h.humidity = hums[i];
            h.is_night = !days.isNull() && (int) days.size() > i &&
                         (days[i] | 1) == 0;
            if (!codes.isNull() && (int) codes.size() > i)
              h.condition = pixoo::WmoToCondition(codes[i] | -1);
            // Hour label: parse "HH" from an ISO "YYYY-MM-DDTHH:MM" timestamp.
            if (!times.isNull() && (int) times.size() > i) {
              const char *ts = times[i];
              if (ts != nullptr) {
                const char *t = std::strchr(ts, 'T');
                if (t != nullptr && std::strlen(t) >= 3)
                  h.hour_of_day = (t[1] - '0') * 10 + (t[2] - '0');
              }
            }
            count = i + 1;
          }
          data.hour_count = count;
        }

        data.valid = data.has_temperature;
        return data.valid;
      });

  if (!ok || !data.valid) {
    ESP_LOGW(TAG, "parse failed");
    return false;
  }
  if (this->impl_->worker.StopRequested())
    return false;
  ESP_LOGD(TAG, "weather updated: %.1f C", data.temperature);
  *out = data;
  return true;
}

}  // namespace esphome::pixoo64::adapters
