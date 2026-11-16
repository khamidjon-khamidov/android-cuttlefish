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

#include "cuttlefish/host/frontend/webrtc/libdevice/car_audio_focus_observer.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "absl/log/log.h"
#include "json/value.h"
#include "json/writer.h"

#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace webrtc_streaming {
namespace {

constexpr char kEventKey[] = "event";
constexpr char kRequestFocus[] = "REQUEST_FOCUS";
constexpr char kAbandonFocus[] = "ABANDON_FOCUS";
constexpr char kGetFocusState[] = "GET_FOCUS_STATE";
constexpr char kFocusChange[] = "FOCUS_CHANGE";
constexpr char kFocusState[] = "FOCUS_STATE";
constexpr char kError[] = "ERROR";

Json::Value FocusMessage(const char* event, int32_t zone_id, int32_t usage,
                         int32_t content_type, const std::string& tags) {
  Json::Value message;
  message[kEventKey] = event;
  message["zone_id"] = zone_id;
  message["usage"] = usage;
  message["content_type"] = content_type;
  message["tags"] = tags;
  return message;
}

}  // namespace

CarAudioFocusObserver::CarAudioFocusObserver(unsigned int port,
                                             unsigned int cid,
                                             bool vhost_user_vsock)
    : cid_(cid),
      port_(port),
      vhost_user_vsock_(vhost_user_vsock),
      is_running_(false),
      last_client_channel_id_(-1) {}

CarAudioFocusObserver::~CarAudioFocusObserver() { Stop(); }

bool CarAudioFocusObserver::Start() {
  if (connection_thread_.joinable()) {
    LOG(ERROR) << "Car audio focus connection thread is already running.";
    return false;
  }

  is_running_ = true;

  connection_thread_ = std::thread([this] {
    while (is_running_) {
      while (cvd_connection_.IsConnected()) {
        ReadServerMessages();
      }

      if (!is_running_) {
        break;
      }
      if (!cvd_connection_.Connect(
              port_, cid_,
              vhost_user_vsock_ ? std::optional(0) : std::nullopt)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        continue;
      }
      LOG(INFO) << "Connected to the guest car audio focus server on vsock "
                   "port "
                << port_;
      Json::Value get_state;
      get_state[kEventKey] = kGetFocusState;
      cvd_connection_.WriteMessage(get_state);
    }

    LOG(INFO) << "Exiting car audio focus connection thread";
  });

  return true;
}

void CarAudioFocusObserver::Stop() {
  is_running_ = false;
  cvd_connection_.Disconnect();
  if (connection_thread_.joinable()) {
    connection_thread_.join();
  }
}

void CarAudioFocusObserver::ReadServerMessages() {
  Json::Value json_value = cvd_connection_.ReadJsonMessage();
  if (json_value.isNull()) {
    return;
  }
  const std::string event = json_value.get(kEventKey, "").asString();
  if (event == kError) {
    LOG(WARNING) << "Guest rejected car audio focus command: "
                 << json_value.get("reason", "").asString();
  } else if (event == kFocusChange || event == kFocusState) {
    LOG(INFO) << "Car audio focus " << event << ": "
              << Json::writeString(Json::StreamWriterBuilder{}, json_value);
  } else {
    LOG(WARNING) << "Unknown car audio focus event from guest: " << event;
    return;
  }

  std::lock_guard<std::mutex> lock(clients_lock_);
  if (event == kFocusState) {
    cached_focus_state_ = json_value;
  }
  for (auto& [_, sender] : client_message_senders_) {
    sender(json_value);
  }
}

Result<void> CarAudioFocusObserver::SendToGuest(const Json::Value& message) {
  CF_EXPECT(cvd_connection_.IsConnected(),
            "Guest car audio focus server is not connected");
  CF_EXPECT(cvd_connection_.WriteMessage(message),
            "Failed to write to the guest car audio focus server");
  return {};
}

Result<void> CarAudioFocusObserver::RequestFocus(int32_t zone_id, int32_t usage,
                                                 int32_t content_type,
                                                 const std::string& tags,
                                                 int32_t focus_gain) {
  Json::Value message =
      FocusMessage(kRequestFocus, zone_id, usage, content_type, tags);
  message["focus_gain"] = focus_gain;
  CF_EXPECT(SendToGuest(message));
  return {};
}

Result<void> CarAudioFocusObserver::AbandonFocus(int32_t zone_id, int32_t usage,
                                                 int32_t content_type,
                                                 const std::string& tags) {
  CF_EXPECT(SendToGuest(
      FocusMessage(kAbandonFocus, zone_id, usage, content_type, tags)));
  return {};
}

int CarAudioFocusObserver::Subscribe(
    std::function<bool(const Json::Value&)> message_sender) {
  std::lock_guard<std::mutex> lock(clients_lock_);
  const int client_id = ++last_client_channel_id_;
  client_message_senders_[client_id] = message_sender;
  if (!cached_focus_state_.isNull()) {
    message_sender(cached_focus_state_);
  }
  return client_id;
}

void CarAudioFocusObserver::Unsubscribe(int message_sender_id) {
  std::lock_guard<std::mutex> lock(clients_lock_);
  client_message_senders_.erase(message_sender_id);
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
