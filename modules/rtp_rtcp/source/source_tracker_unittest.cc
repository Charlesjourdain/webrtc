/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/source_tracker.h"

#include <algorithm>
#include <list>
#include <random>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "api/rtp_headers.h"
#include "api/rtp_packet_info.h"
#include "api/rtp_packet_infos.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::Combine;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::SizeIs;
using ::testing::TestWithParam;
using ::testing::Values;

constexpr size_t kPacketInfosCountMax = 5;

// Simple "guaranteed to be correct" re-implementation of |SourceTracker| for
// dual-implementation testing purposes.
class ExpectedSourceTracker {
 public:
  explicit ExpectedSourceTracker(Clock* clock) : clock_(clock) {}

  void OnFrameDelivered(const RtpPacketInfos& packet_infos) {
    const int64_t now_ms = clock_->TimeInMilliseconds();

    for (const auto& packet_info : packet_infos) {
      for (const auto& csrc : packet_info.csrcs()) {
        entries_.emplace_front(now_ms, csrc, RtpSourceType::CSRC,
                               packet_info.audio_level(),
                               packet_info.rtp_timestamp());
      }

      entries_.emplace_front(now_ms, packet_info.ssrc(), RtpSourceType::SSRC,
                             packet_info.audio_level(),
                             packet_info.rtp_timestamp());
    }

    PruneEntries(now_ms);
  }

  std::vector<RtpSource> GetSources() const {
    PruneEntries(clock_->TimeInMilliseconds());

    return std::vector<RtpSource>(entries_.begin(), entries_.end());
  }

 private:
  void PruneEntries(int64_t now_ms) const {
    const int64_t prune_ms = now_ms - 10000;  // 10 seconds

    std::set<std::pair<RtpSourceType, uint32_t>> seen;

    auto it = entries_.begin();
    auto end = entries_.end();
    while (it != end) {
      auto next = it;
      ++next;

      auto key = std::make_pair(it->source_type(), it->source_id());
      if (!seen.insert(key).second || it->timestamp_ms() < prune_ms) {
        entries_.erase(it);
      }

      it = next;
    }
  }

  Clock* const clock_;

  mutable std::list<RtpSource> entries_;
};

class SourceTrackerRandomTest
    : public TestWithParam<std::tuple<uint32_t, uint32_t>> {
 protected:
  SourceTrackerRandomTest()
      : ssrcs_count_(std::get<0>(GetParam())),
        csrcs_count_(std::get<1>(GetParam())),
        generator_(42) {}

  RtpPacketInfos GeneratePacketInfos() {
    size_t count = std::uniform_int_distribution<size_t>(
        1, kPacketInfosCountMax)(generator_);

    RtpPacketInfos::vector_type packet_infos;
    for (size_t i = 0; i < count; ++i) {
      packet_infos.emplace_back(GenerateSsrc(), GenerateCsrcs(),
                                GenerateRtpTimestamp(), GenerateAudioLevel(),
                                GenerateReceiveTimeMs());
    }

    return RtpPacketInfos(std::move(packet_infos));
  }

  int64_t GenerateClockAdvanceTimeMilliseconds() {
    double roll = std::uniform_real_distribution<double>(0.0, 1.0)(generator_);

    if (roll < 0.05) {
      return 0;
    }

    if (roll < 0.08) {
      return SourceTracker::kTimeoutMs - 1;
    }

    if (roll < 0.11) {
      return SourceTracker::kTimeoutMs;
    }

    if (roll < 0.19) {
      return std::uniform_int_distribution<int64_t>(
          SourceTracker::kTimeoutMs,
          SourceTracker::kTimeoutMs * 1000)(generator_);
    }

    return std::uniform_int_distribution<int64_t>(
        1, SourceTracker::kTimeoutMs - 1)(generator_);
  }

 private:
  uint32_t GenerateSsrc() {
    return std::uniform_int_distribution<uint32_t>(1, ssrcs_count_)(generator_);
  }

  std::vector<uint32_t> GenerateCsrcs() {
    std::vector<uint32_t> csrcs;
    for (size_t i = 1; i <= csrcs_count_ && csrcs.size() < kRtpCsrcSize; ++i) {
      if (std::bernoulli_distribution(0.5)(generator_)) {
        csrcs.push_back(i);
      }
    }

    return csrcs;
  }

  uint32_t GenerateRtpTimestamp() {
    return std::uniform_int_distribution<uint32_t>()(generator_);
  }

  absl::optional<uint8_t> GenerateAudioLevel() {
    if (std::bernoulli_distribution(0.25)(generator_)) {
      return absl::nullopt;
    }

    // Workaround for std::uniform_int_distribution<uint8_t> not being allowed.
    return static_cast<uint8_t>(
        std::uniform_int_distribution<uint16_t>()(generator_));
  }

  int64_t GenerateReceiveTimeMs() {
    return std::uniform_int_distribution<int64_t>()(generator_);
  }

  const uint32_t ssrcs_count_;
  const uint32_t csrcs_count_;

  std::mt19937 generator_;
};

}  // namespace

TEST_P(SourceTrackerRandomTest, RandomOperations) {
  constexpr size_t kIterationsCount = 200;

  SimulatedClock clock(1000000000000ULL);
  SourceTracker actual_tracker(&clock);
  ExpectedSourceTracker expected_tracker(&clock);

  ASSERT_THAT(actual_tracker.GetSources(), IsEmpty());
  ASSERT_THAT(expected_tracker.GetSources(), IsEmpty());

  for (size_t i = 0; i < kIterationsCount; ++i) {
    RtpPacketInfos packet_infos = GeneratePacketInfos();

    actual_tracker.OnFrameDelivered(packet_infos);
    expected_tracker.OnFrameDelivered(packet_infos);

    clock.AdvanceTimeMilliseconds(GenerateClockAdvanceTimeMilliseconds());

    ASSERT_THAT(actual_tracker.GetSources(),
                ElementsAreArray(expected_tracker.GetSources()));
  }
}

INSTANTIATE_TEST_SUITE_P(,
                         SourceTrackerRandomTest,
                         Combine(/*ssrcs_count_=*/Values(1, 2, 4),
                                 /*csrcs_count_=*/Values(0, 1, 3, 7)));

TEST(SourceTrackerTest, StartEmpty) {
  SimulatedClock clock(1000000000000ULL);
  SourceTracker tracker(&clock);

  EXPECT_THAT(tracker.GetSources(), IsEmpty());
}

TEST(SourceTrackerTest, OnFrameDeliveredRecordsSources) {
  constexpr uint32_t kSsrc = 10;
  constexpr uint32_t kCsrcs0 = 20;
  constexpr uint32_t kCsrcs1 = 21;
  constexpr uint32_t kRtpTimestamp = 40;
  constexpr absl::optional<uint8_t> kAudioLevel = 50;
  constexpr int64_t kReceiveTimeMs = 60;

  SimulatedClock clock(1000000000000ULL);
  SourceTracker tracker(&clock);

  tracker.OnFrameDelivered(RtpPacketInfos({RtpPacketInfo(
      kSsrc, {kCsrcs0, kCsrcs1}, kRtpTimestamp, kAudioLevel, kReceiveTimeMs)}));

  int64_t timestamp_ms = clock.TimeInMilliseconds();

  EXPECT_THAT(tracker.GetSources(),
              ElementsAre(RtpSource(timestamp_ms, kSsrc, RtpSourceType::SSRC,
                                    kAudioLevel, kRtpTimestamp),
                          RtpSource(timestamp_ms, kCsrcs1, RtpSourceType::CSRC,
                                    kAudioLevel, kRtpTimestamp),
                          RtpSource(timestamp_ms, kCsrcs0, RtpSourceType::CSRC,
                                    kAudioLevel, kRtpTimestamp)));
}

TEST(SourceTrackerTest, OnFrameDeliveredUpdatesSources) {
  constexpr uint32_t kSsrc = 10;
  constexpr uint32_t kCsrcs0 = 20;
  constexpr uint32_t kCsrcs1 = 21;
  constexpr uint32_t kCsrcs2 = 22;
  constexpr uint32_t kRtpTimestamp0 = 40;
  constexpr uint32_t kRtpTimestamp1 = 41;
  constexpr absl::optional<uint8_t> kAudioLevel0 = 50;
  constexpr absl::optional<uint8_t> kAudioLevel1 = absl::nullopt;
  constexpr int64_t kReceiveTimeMs0 = 60;
  constexpr int64_t kReceiveTimeMs1 = 61;

  SimulatedClock clock(1000000000000ULL);
  SourceTracker tracker(&clock);

  tracker.OnFrameDelivered(
      RtpPacketInfos({RtpPacketInfo(kSsrc, {kCsrcs0, kCsrcs1}, kRtpTimestamp0,
                                    kAudioLevel0, kReceiveTimeMs0)}));

  int64_t timestamp_ms_0 = clock.TimeInMilliseconds();

  clock.AdvanceTimeMilliseconds(17);

  tracker.OnFrameDelivered(
      RtpPacketInfos({RtpPacketInfo(kSsrc, {kCsrcs0, kCsrcs2}, kRtpTimestamp1,
                                    kAudioLevel1, kReceiveTimeMs1)}));

  int64_t timestamp_ms_1 = clock.TimeInMilliseconds();

  EXPECT_THAT(
      tracker.GetSources(),
      ElementsAre(RtpSource(timestamp_ms_1, kSsrc, RtpSourceType::SSRC,
                            kAudioLevel1, kRtpTimestamp1),
                  RtpSource(timestamp_ms_1, kCsrcs2, RtpSourceType::CSRC,
                            kAudioLevel1, kRtpTimestamp1),
                  RtpSource(timestamp_ms_1, kCsrcs0, RtpSourceType::CSRC,
                            kAudioLevel1, kRtpTimestamp1),
                  RtpSource(timestamp_ms_0, kCsrcs1, RtpSourceType::CSRC,
                            kAudioLevel0, kRtpTimestamp0)));
}

TEST(SourceTrackerTest, TimedOutSourcesAreRemoved) {
  constexpr uint32_t kSsrc = 10;
  constexpr uint32_t kCsrcs0 = 20;
  constexpr uint32_t kCsrcs1 = 21;
  constexpr uint32_t kCsrcs2 = 22;
  constexpr uint32_t kRtpTimestamp0 = 40;
  constexpr uint32_t kRtpTimestamp1 = 41;
  constexpr absl::optional<uint8_t> kAudioLevel0 = 50;
  constexpr absl::optional<uint8_t> kAudioLevel1 = absl::nullopt;
  constexpr int64_t kReceiveTimeMs0 = 60;
  constexpr int64_t kReceiveTimeMs1 = 61;

  SimulatedClock clock(1000000000000ULL);
  SourceTracker tracker(&clock);

  tracker.OnFrameDelivered(
      RtpPacketInfos({RtpPacketInfo(kSsrc, {kCsrcs0, kCsrcs1}, kRtpTimestamp0,
                                    kAudioLevel0, kReceiveTimeMs0)}));

  clock.AdvanceTimeMilliseconds(17);

  tracker.OnFrameDelivered(
      RtpPacketInfos({RtpPacketInfo(kSsrc, {kCsrcs0, kCsrcs2}, kRtpTimestamp1,
                                    kAudioLevel1, kReceiveTimeMs1)}));

  int64_t timestamp_ms_1 = clock.TimeInMilliseconds();

  clock.AdvanceTimeMilliseconds(SourceTracker::kTimeoutMs);

  EXPECT_THAT(
      tracker.GetSources(),
      ElementsAre(RtpSource(timestamp_ms_1, kSsrc, RtpSourceType::SSRC,
                            kAudioLevel1, kRtpTimestamp1),
                  RtpSource(timestamp_ms_1, kCsrcs2, RtpSourceType::CSRC,
                            kAudioLevel1, kRtpTimestamp1),
                  RtpSource(timestamp_ms_1, kCsrcs0, RtpSourceType::CSRC,
                            kAudioLevel1, kRtpTimestamp1)));
}

}  // namespace webrtc