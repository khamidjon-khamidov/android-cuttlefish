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

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "json/json.h"

#include "cuttlefish/common/libs/utils/vsock_connection.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace webrtc_streaming {

// Connects to the guest AudioControl HAL over vsock and lets the host act as
// an external audio source taking part in car audio focus arbitration. The
// guest (CarAudioService) owns focus state; this class relays requests to it
// and mirrors the focus changes it reports.
class CarAudioFocusObserver {
 public:
  CarAudioFocusObserver(unsigned int port, unsigned int cid,
                        bool vhost_user_vsock);
  ~CarAudioFocusObserver();

  CarAudioFocusObserver(const CarAudioFocusObserver&) = delete;
  CarAudioFocusObserver& operator=(const CarAudioFocusObserver&) = delete;

  bool Start();

  Result<void> RequestFocus(int32_t zone_id, int32_t usage,
                            int32_t content_type, const std::string& tags,
                            int32_t focus_gain);
  Result<void> AbandonFocus(int32_t zone_id, int32_t usage,
                            int32_t content_type, const std::string& tags);

  int Subscribe(std::function<bool(const Json::Value&)> message_sender);
  void Unsubscribe(int message_sender_id);

 private:
  void Stop();
  void ReadServerMessages();
  Result<void> SendToGuest(const Json::Value& message);

  VsockClientConnection cvd_connection_;
  unsigned int cid_;
  unsigned int port_;
  bool vhost_user_vsock_;
  std::thread connection_thread_;
  std::atomic<bool> is_running_;

  std::mutex clients_lock_;
  Json::Value cached_focus_state_;
  std::unordered_map<int, std::function<bool(const Json::Value&)>>
      client_message_senders_;
  int last_client_channel_id_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
