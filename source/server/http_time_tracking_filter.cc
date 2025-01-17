#include "source/server/http_time_tracking_filter.h"

#include <string>

#include "envoy/server/filter_config.h"

#include "source/common/thread_safe_monotonic_time_stopwatch.h"
#include "source/server/configuration.h"
#include "source/server/well_known_headers.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace Nighthawk {
namespace Server {

HttpTimeTrackingFilterConfig::HttpTimeTrackingFilterConfig(
    const nighthawk::server::TimeTrackingConfiguration& proto_config)
    : FilterConfigurationBase(proto_config.experimental_response_options(), "time-tracking"),
      stopwatch_(std::make_unique<ThreadSafeMontonicTimeStopwatch>()) {}

uint64_t
HttpTimeTrackingFilterConfig::getElapsedNanosSinceLastRequest(Envoy::TimeSource& time_source) {
  return stopwatch_->getElapsedNsAndReset(time_source);
}

HttpTimeTrackingFilter::HttpTimeTrackingFilter(HttpTimeTrackingFilterConfigSharedPtr config)
    : config_(std::move(config)) {}

Envoy::Http::FilterHeadersStatus
HttpTimeTrackingFilter::decodeHeaders(Envoy::Http::RequestHeaderMap& headers, bool end_stream) {
  effective_config_ = config_->computeEffectiveConfiguration(headers);
  if (end_stream && config_->validateOrSendError(effective_config_, *decoder_callbacks_)) {
    return Envoy::Http::FilterHeadersStatus::StopIteration;
  }
  return Envoy::Http::FilterHeadersStatus::Continue;
}

Envoy::Http::FilterDataStatus HttpTimeTrackingFilter::decodeData(Envoy::Buffer::Instance&,
                                                                 bool end_stream) {
  if (end_stream && config_->validateOrSendError(effective_config_, *decoder_callbacks_)) {
    return Envoy::Http::FilterDataStatus::StopIterationNoBuffer;
  }
  return Envoy::Http::FilterDataStatus::Continue;
}

Envoy::Http::FilterHeadersStatus
HttpTimeTrackingFilter::encodeHeaders(Envoy::Http::ResponseHeaderMap& response_headers, bool) {
  if (effective_config_.ok()) {
    const std::string previous_request_delta_in_response_header =
        effective_config_.value()->emit_previous_request_delta_in_response_header();
    if (!previous_request_delta_in_response_header.empty() && last_request_delta_ns_ > 0) {
      response_headers.appendCopy(
          Envoy::Http::LowerCaseString(previous_request_delta_in_response_header),
          absl::StrCat(last_request_delta_ns_));
    }
  }
  return Envoy::Http::FilterHeadersStatus::Continue;
}

void HttpTimeTrackingFilter::setDecoderFilterCallbacks(
    Envoy::Http::StreamDecoderFilterCallbacks& callbacks) {
  Envoy::Http::PassThroughFilter::setDecoderFilterCallbacks(callbacks);
  last_request_delta_ns_ =
      config_->getElapsedNanosSinceLastRequest(callbacks.dispatcher().timeSource());
}

} // namespace Server
} // namespace Nighthawk
