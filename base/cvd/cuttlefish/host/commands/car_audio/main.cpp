/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/base/no_destructor.h"

#include "cuttlefish/common/libs/utils/tee_logging.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/host/libs/command_util/runner/run_cvd.pb.h"
#include "cuttlefish/host/libs/command_util/util.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

static const char kUsage[] =
    R"(

Act as an external audio source in a Cuttlefish automotive device's car audio
focus arbitration (via the guest AudioControl HAL).

usage: cvd car_audio <subcommand> <args>

Commands (cvd car_audio help <subcommand> for more information):

    request_focus: Requests car audio focus for an external audio source.

    abandon_focus: Abandons car audio focus held by an external audio source.
)";

static const char kRequestFocusUsage[] =
    R"(
Requests car audio focus on behalf of an external (non-Android) audio source.

usage: cvd car_audio request_focus \
        [--zone_id=<id>] [--usage=<AudioUsage>] [--content_type=<type>] \
        [--tags=<tags>] [--focus_gain=<AudioFocusChange>]

Defaults emulate navigation guidance: zone 0, usage 12
(ASSISTANCE_NAVIGATION_GUIDANCE), content_type 1 (SPEECH), focus_gain 3
(GAIN_TRANSIENT_MAY_DUCK).

AudioUsage: 1 MEDIA, 2 VOICE_COMMUNICATION, 4 ALARM, 5 NOTIFICATION,
            12 ASSISTANCE_NAVIGATION_GUIDANCE, 16 ASSISTANT, 1000 EMERGENCY,
            1001 SAFETY, 1002 VEHICLE_STATUS, 1003 ANNOUNCEMENT
AudioFocusChange: 1 GAIN, 2 GAIN_TRANSIENT, 3 GAIN_TRANSIENT_MAY_DUCK,
                  4 GAIN_TRANSIENT_EXCLUSIVE
)";

static const char kAbandonFocusUsage[] =
    R"(
Abandons car audio focus previously requested with request_focus. The
metadata must match the request.

usage: cvd car_audio abandon_focus \
        [--zone_id=<id>] [--usage=<AudioUsage>] [--content_type=<type>] \
        [--tags=<tags>]
)";

struct FocusArgs {
  int instance_num = 1;
  int zone_id = 0;
  int usage = 12;
  int content_type = 1;
  std::string tags;
  int focus_gain = 3;
};

Result<FocusArgs> ParseFocusArgs(std::vector<std::string>& args,
                                 const char* usage_text) {
  FocusArgs parsed;
  const std::vector<Flag> flags = {
      GflagsCompatFlag("instance_num", parsed.instance_num),
      GflagsCompatFlag("zone_id", parsed.zone_id).Help("Car audio zone id."),
      GflagsCompatFlag("usage", parsed.usage)
          .Help("android.media.audio.common.AudioUsage value."),
      GflagsCompatFlag("content_type", parsed.content_type)
          .Help("android.media.audio.common.AudioContentType value."),
      GflagsCompatFlag("tags", parsed.tags)
          .Help("Optional vendor tag, e.g. com.google.strategy=VR."),
      GflagsCompatFlag("focus_gain", parsed.focus_gain)
          .Help("AudioFocusChange gain value (request_focus only)."),
  };
  auto parse_res = ConsumeFlags(flags, args);
  if (!parse_res.has_value()) {
    std::cerr << parse_res.error() << std::endl;
    std::cerr << "Failed to parse flags. Usage:" << std::endl;
    std::cerr << usage_text << std::endl;
    return CF_ERR("Failed to parse flags");
  }
  CF_EXPECT(args.empty(), "Unexpected arguments. Usage:" << usage_text);
  return parsed;
}

Result<int> SendAction(int instance_num,
                       const run_cvd::ExtendedLauncherAction& action) {
  auto config = CuttlefishConfig::Get();
  CF_EXPECT(config != nullptr, "Failed to get Cuttlefish config.");
  auto socket = CF_EXPECT(
      GetLauncherMonitor(*config, instance_num, /*timeout_seconds=*/5));
  CF_EXPECT(RunLauncherAction(socket, action, std::nullopt),
            "Failed to get success response from launcher.");
  return 0;
}

Result<int> DoHelp(std::vector<std::string>& args) {
  if (args.empty()) {
    std::cerr << kUsage << std::endl;
    return 0;
  }

  static const absl::NoDestructor<std::unordered_map<std::string, std::string>>
      kSubCommandUsages({
          {"request_focus", kRequestFocusUsage},
          {"abandon_focus", kAbandonFocusUsage},
      });

  const std::string& subcommand_str = args[0];
  auto subcommand_usage = kSubCommandUsages->find(subcommand_str);
  if (subcommand_usage == kSubCommandUsages->end()) {
    std::cerr << "Unknown subcommand '" << subcommand_str
              << "'. See `cvd car_audio help`" << std::endl;
    return 1;
  }

  std::cout << subcommand_usage->second << std::endl;
  return 0;
}

Result<int> DoRequestFocus(std::vector<std::string>& args) {
  const FocusArgs focus = CF_EXPECT(ParseFocusArgs(args, kRequestFocusUsage));

  run_cvd::ExtendedLauncherAction extended_action;
  auto* request = extended_action.mutable_request_car_audio_focus();
  request->set_zone_id(focus.zone_id);
  request->set_usage(focus.usage);
  request->set_content_type(focus.content_type);
  request->set_tags(focus.tags);
  request->set_focus_gain(focus.focus_gain);

  std::cout << "Requesting car audio focus " << focus.focus_gain << " in zone "
            << focus.zone_id << " for usage " << focus.usage
            << ", content type " << focus.content_type << "." << std::endl;
  return CF_EXPECT(SendAction(focus.instance_num, extended_action));
}

Result<int> DoAbandonFocus(std::vector<std::string>& args) {
  const FocusArgs focus = CF_EXPECT(ParseFocusArgs(args, kAbandonFocusUsage));

  run_cvd::ExtendedLauncherAction extended_action;
  auto* request = extended_action.mutable_abandon_car_audio_focus();
  request->set_zone_id(focus.zone_id);
  request->set_usage(focus.usage);
  request->set_content_type(focus.content_type);
  request->set_tags(focus.tags);

  std::cout << "Abandoning car audio focus in zone " << focus.zone_id
            << " for usage " << focus.usage << ", content type "
            << focus.content_type << "." << std::endl;
  return CF_EXPECT(SendAction(focus.instance_num, extended_action));
}

using CarAudioSubCommand = Result<int> (*)(std::vector<std::string>&);

int CarAudioMain(int argc, char** argv) {
  cuttlefish::LogToStderr();
  const std::unordered_map<std::string, CarAudioSubCommand> kSubCommands = {
      {"help", DoHelp},
      {"request_focus", DoRequestFocus},
      {"abandon_focus", DoAbandonFocus},
  };

  std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty()) {
    args.push_back("help");
  }

  const std::string command_str = args[0];
  args.erase(args.begin());

  auto command_func_it = kSubCommands.find(command_str);
  if (command_func_it == kSubCommands.end()) {
    std::cerr << "Unknown car_audio command: '" << command_str << "'."
              << std::endl;
    return 1;
  }

  auto result = command_func_it->second(args);
  if (!result.has_value()) {
    std::cerr << result.error();
    return 1;
  }
  return result.value();
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) { return cuttlefish::CarAudioMain(argc, argv); }
