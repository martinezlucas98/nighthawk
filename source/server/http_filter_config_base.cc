#include "source/server/http_filter_config_base.h"

#include "source/server/well_known_headers.h"

namespace Nighthawk {
namespace Server {

FilterConfigurationBase::FilterConfigurationBase(
    const nighthawk::server::ResponseOptions& proto_config, absl::string_view filter_name)
    : filter_name_(filter_name),
      server_config_(std::make_shared<nighthawk::server::ResponseOptions>(proto_config)) {}

const absl::StatusOr<EffectiveFilterConfigurationPtr>
FilterConfigurationBase::computeEffectiveConfiguration(
    const Envoy::Http::RequestHeaderMap& headers) {
  const auto& request_config_header = headers.get(TestServer::HeaderNames::get().TestServerConfig);
  if (request_config_header.size() == 1) {
    // We could be more flexible and look for the first request header that has a value,
    // but without a proper understanding of a real use case for that, we are assuming that any
    // existence of duplicate headers here is an error.
    nighthawk::server::ResponseOptions response_options = *server_config_;
    std::string error_message;
    if (Configuration::mergeJsonConfig(request_config_header[0]->value().getStringView(),
                                       response_options, error_message)) {
      return std::make_shared<const nighthawk::server::ResponseOptions>(
          std::move(response_options));
    } else {
      return absl::InvalidArgumentError(error_message);
    }
  } else if (request_config_header.size() > 1) {
    return absl::InvalidArgumentError(
        "Received multiple configuration headers in the request, expected only one.");
  }
  return server_config_;
}

bool FilterConfigurationBase::validateOrSendError(
    absl::StatusOr<EffectiveFilterConfigurationPtr>& effective_config,
    Envoy::Http::StreamDecoderFilterCallbacks& decoder_callbacks) const {
  if (!effective_config.ok()) {
    decoder_callbacks.sendLocalReply(static_cast<Envoy::Http::Code>(500),
                                     fmt::format("{} didn't understand the request: {}",
                                                 filter_name_, effective_config.status().message()),
                                     nullptr, absl::nullopt, "");
    return true;
  }
  return false;
}

} // namespace Server
} // namespace Nighthawk
