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

#include "cuttlefish/host/frontend/webrtc/audio_channel_matrix.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace cuttlefish {
namespace {

TEST(AudioChannelMatrixTest, StereoToStereoIsDiagonalVolume) {
  const std::vector<std::vector<float>> matrix =
      BuildChannelMixingMatrix(/* dst_channels */ 2, /* src_channels */ 2,
                               /* volume */ 0.5f);

  const std::vector<std::vector<float>> expected = {
      {0.5f, 0.0f},
      {0.0f, 0.5f},
  };
  EXPECT_EQ(matrix, expected);
}

TEST(AudioChannelMatrixTest, MonoToStereoOnlyFeedsFirstChannel) {
  const std::vector<std::vector<float>> matrix =
      BuildChannelMixingMatrix(/* dst_channels */ 2, /* src_channels */ 1,
                               /* volume */ 1.0f);

  const std::vector<std::vector<float>> expected = {
      {1.0f},
      {0.0f},
  };
  EXPECT_EQ(matrix, expected);
}

TEST(AudioChannelMatrixTest, Surround51ToStereoDropsExtraChannels) {
  const std::vector<std::vector<float>> matrix =
      BuildChannelMixingMatrix(/* dst_channels */ 2, /* src_channels */ 6,
                               /* volume */ 1.0f);

  const std::vector<std::vector<float>> expected = {
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
  };
  EXPECT_EQ(matrix, expected);
}

TEST(AudioChannelMatrixTest, Surround51ToSurround51IsDiagonalVolume) {
  constexpr float kVolume = 0.25f;
  const std::vector<std::vector<float>> matrix = BuildChannelMixingMatrix(
      /* dst_channels */ 6, /* src_channels */ 6, kVolume);

  ASSERT_EQ(matrix.size(), 6);
  for (size_t i = 0; i < matrix.size(); ++i) {
    ASSERT_EQ(matrix[i].size(), 6);
    for (size_t j = 0; j < matrix[i].size(); ++j) {
      EXPECT_FLOAT_EQ(matrix[i][j], i == j ? kVolume : 0.0f);
    }
  }
}

TEST(AudioChannelMatrixTest, ZeroVolumeSilencesEveryChannel) {
  const std::vector<std::vector<float>> matrix =
      BuildChannelMixingMatrix(/* dst_channels */ 2, /* src_channels */ 2,
                               /* volume */ 0.0f);

  const std::vector<std::vector<float>> expected = {
      {0.0f, 0.0f},
      {0.0f, 0.0f},
  };
  EXPECT_EQ(matrix, expected);
}

}  // namespace
}  // namespace cuttlefish
