#pragma once

#include <atomic>
#include <string>

#include "envoy/server/filter_config.h"

#include "external/envoy/source/extensions/filters/http/fault/fault_filter.h"

#include "api/server/response_options.pb.h"

#include "source/server/http_filter_config_base.h"

namespace Nighthawk {
namespace Server {

/**
 * Filter configuration container class for the dynamic delay extension.
 * Instances of this class will be shared accross instances of HttpDynamicDelayDecoderFilter.
 * The methods for getting and manipulating (global) active filter instance counts are thread safe.
 */
class HttpDynamicDelayDecoderFilterConfig : public FilterConfigurationBase {

public:
  /**
   * Constructs a new HttpDynamicDelayDecoderFilterConfig instance.
   *
   * @param proto_config The proto configuration of the filter. Will be translated internally into
   * the right configuration for the base class structure (the fault filter and config).
   * @param runtime Envoy runtime to be used by the filter.
   * @param stats_prefix Prefix to use by the filter when it names statistics. E.g.
   * dynamic-delay.fault.delays_injected: 1
   * @param scope Statistics scope to be used by the filter.
   * @param time_source Time source to be used by the filter.
   */
  HttpDynamicDelayDecoderFilterConfig(
      const nighthawk::server::DynamicDelayConfiguration& proto_config,
      Envoy::Runtime::Loader& runtime, const std::string& stats_prefix, Envoy::Stats::Scope& scope,
      Envoy::TimeSource& time_source);
  /**
   * Increments the number of globally active filter instances.
   */
  void incrementFilterInstanceCount() { instances()++; }

  /**
   * Decrements the number of globally active filter instances.
   */
  void decrementFilterInstanceCount() { instances()--; }

  /**
   * @return uint64_t the approximate number of globally active HttpDynamicDelayDecoderFilter
   * instances. Approximate, because by the time the value is consumed it might have changed.
   */
  uint64_t approximateFilterInstances() const { return instances(); }

  /**
   * @return Envoy::Runtime::Loader& to be used by filter instantiations associated to this.
   */
  Envoy::Runtime::Loader& runtime() { return runtime_; }

  /**
   * @return Envoy::Stats::Scope& to be used by filter instantiations associated to this.
   */
  Envoy::Stats::Scope& scope() { return scope_; }

  /**
   * @return Envoy::TimeSource& to be used by filter instantiations associated to this.
   */
  Envoy::TimeSource& time_source() { return time_source_; }

  /**
   * @return std::string to be used by filter instantiations associated to this.
   */
  std::string stats_prefix() { return stats_prefix_; }

private:
  static std::atomic<uint64_t>& instances() {
    // We lazy-init the atomic to avoid static initialization / a fiasco.
    MUTABLE_CONSTRUCT_ON_FIRST_USE(std::atomic<uint64_t>, 0); // NOLINT
  }

  Envoy::Runtime::Loader& runtime_;
  const std::string stats_prefix_;
  Envoy::Stats::Scope& scope_;
  Envoy::TimeSource& time_source_;
};

using HttpDynamicDelayDecoderFilterConfigSharedPtr =
    std::shared_ptr<HttpDynamicDelayDecoderFilterConfig>;

/**
 * Extension that controls the fault filter extension by supplying it with a request
 * header that triggers it to induce a delay under the hood.
 * In the future, we may look into injecting the fault filter ourselves with the right
 * configuration, either directly/verbatim, or by including a derivation of it in
 * this code base, thereby making it all transparant to the user / eliminating the need
 * to configure the fault filter and make NH take full ownership at the feature level.
 */
class HttpDynamicDelayDecoderFilter : public Envoy::Extensions::HttpFilters::Fault::FaultFilter {
public:
  HttpDynamicDelayDecoderFilter(HttpDynamicDelayDecoderFilterConfigSharedPtr);
  ~HttpDynamicDelayDecoderFilter() override;

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Envoy::Http::FilterHeadersStatus decodeHeaders(Envoy::Http::RequestHeaderMap&, bool) override;
  Envoy::Http::FilterDataStatus decodeData(Envoy::Buffer::Instance&, bool) override;
  void setDecoderFilterCallbacks(Envoy::Http::StreamDecoderFilterCallbacks&) override;

  /**
   * Compute the concurrency based linear delay in milliseconds.
   *
   * @param concurrency indicating the number of concurrently active requests.
   * @param minimal_delay gets unconditionally included in the return value.
   * @param delay_factor added for each increase in the number of active requests.
   * @return int64_t the computed delay in milliseconds.
   */
  static int64_t
  computeConcurrencyBasedLinearDelayMs(const uint64_t concurrency,
                                       const Envoy::ProtobufWkt::Duration& minimal_delay,
                                       const Envoy::ProtobufWkt::Duration& delay_factor) {
    return std::round(Envoy::Protobuf::util::TimeUtil::DurationToNanoseconds(
                          minimal_delay + (concurrency * delay_factor)) /
                      1e6);
  }

  /**
   * Compute the delay in milliseconds, based on provided response options and number of active
   * requests.
   *
   * @param response_options Response options configuration.
   * @param concurrency The number of concurrenct active requests.
   * @return absl::optional<int64_t> The computed delay in milliseconds, if any.
   */
  static absl::optional<int64_t>
  computeDelayMs(const nighthawk::server::ResponseOptions& response_options,
                 const uint64_t concurrency);

  /**
   * Communicate to the fault filter, which should be running after this filter, that a delay should
   * be inserted. The request is only made when the passed delay is set to a value > 0.
   *
   * @param delay_ms The delay in milliseconds that should be propagated, if any. When not set or <=
   * 0, the call will be a no-op.
   * @param request_headers The request headers that will be modified to instruct the fault filter.
   */
  static void maybeRequestFaultFilterDelay(const absl::optional<int64_t> delay_ms,
                                           Envoy::Http::RequestHeaderMap& request_headers);

  /**
   * Translates our options into a configuration of the fault filter base class where needed.
   *
   * @param config Dynamic delay configuration.
   * @return Envoy::Extensions::HttpFilters::Fault::FaultFilterConfigSharedPtr
   */
  static Envoy::Extensions::HttpFilters::Fault::FaultFilterConfigSharedPtr
  translateOurConfigIntoFaultFilterConfig(HttpDynamicDelayDecoderFilterConfig& config);

private:
  const HttpDynamicDelayDecoderFilterConfigSharedPtr config_;
  absl::StatusOr<EffectiveFilterConfigurationPtr> effective_config_;
  Envoy::Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
  bool destroyed_{false};
};

} // namespace Server
} // namespace Nighthawk
