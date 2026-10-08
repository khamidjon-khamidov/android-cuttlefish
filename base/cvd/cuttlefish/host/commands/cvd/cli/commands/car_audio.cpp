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

#include "cuttlefish/host/commands/cvd/cli/commands/car_audio.h"

#include <signal.h>  // IWYU pragma: keep
#include <stdlib.h>

#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector.h"
#include "cuttlefish/host/commands/cvd/cli/utils.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kCarAudioBin[] = "cvd_internal_car_audio";

constexpr char kSummaryHelpText[] =
    R"(Acts as an external audio source in a car audio focus arbitration)";

constexpr char kDetailedHelpText[] =
    R"(

usage: cvd car_audio <command> <args>

Commands:
    help <command>      Print help for a command.
    request_focus       Requests car audio focus for an external audio source.
    abandon_focus       Abandons car audio focus held by an external source.
)";

Result<Command> BuildCommand(
    InstanceManager& instance_manager, const CommandRequest& request,
    std::vector<std::string>& subcmd_args,
    std::unordered_map<std::string, std::string> envs) {
  int instance_num = -1;
  Flag instance_num_flag = GflagsCompatFlag("instance_num", instance_num);

  CF_EXPECT(ConsumeFlags({instance_num_flag}, subcmd_args));

  auto [instance, group] =
      instance_num >= 0
          ? CF_EXPECT(instance_manager.FindInstanceWithGroup(
                {.instance_id = instance_num}))
          : CF_EXPECT(selector::SelectInstance(instance_manager, request));
  const auto& home = group.Proto().home_directory();

  const std::string& android_host_out = group.Proto().host_artifacts_path();
  const std::string bin_path =
      absl::StrCat(android_host_out, "/bin/", kCarAudioBin);

  std::vector<std::string> cvd_env_args{subcmd_args};
  cvd_env_args.push_back(absl::StrCat("--instance_num=", instance.Id()));
  envs["HOME"] = home;
  envs[kAndroidHostOut] = android_host_out;
  envs[kAndroidSoongHostOut] = android_host_out;

  ConstructCommandParam construct_cmd_param{.bin_path = bin_path,
                                            .home = home,
                                            .args = cvd_env_args,
                                            .envs = std::move(envs),
                                            .working_dir = CurrentDirectory(),
                                            .command_name = kCarAudioBin};
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
  return command;
}

}  // namespace

CvdCarAudioCommandHandler::CvdCarAudioCommandHandler(
    InstanceManager& instance_manager)
    : instance_manager_{instance_manager} {}

Result<void> CvdCarAudioCommandHandler::Handle(const CommandRequest& request) {
  const std::unordered_map<std::string, std::string>& env = request.Env();

  std::vector<std::string> subcmd_args = request.SubcommandArguments();
  if (subcmd_args.empty()) {
    std::cerr << kDetailedHelpText;
    return {};
  }

  Command command =
      CF_EXPECT(BuildCommand(instance_manager_, request, subcmd_args, env));

  // NOLINTNEXTLINE(misc-include-cleaner)
  siginfo_t infop = CF_EXPECT(command.Start().Wait(WEXITED));

  CF_EXPECT(CheckProcessExitedNormally(infop));
  return {};
}

std::vector<std::string> CvdCarAudioCommandHandler::CmdList() const {
  return {"car_audio"};
}

std::string CvdCarAudioCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdCarAudioCommandHandler::RequiresDeviceExists() const { return true; }

Result<std::string> CvdCarAudioCommandHandler::DetailedHelp(
    const CommandRequest& request) {
  return kDetailedHelpText;
}

}  // namespace cuttlefish
