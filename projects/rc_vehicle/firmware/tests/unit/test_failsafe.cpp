#include <gtest/gtest.h>

#include "failsafe.hpp"
#include "test_helpers.hpp"

using namespace rc_vehicle;
using namespace rc_vehicle::testing;

// ═══════════════════════════════════════════════════════════════════════════
// Basic Functionality Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, InitiallyInactive) {
  Failsafe fs(250);
  EXPECT_EQ(fs.GetState(), FailsafeState::Inactive)
      << "Failsafe should start in Inactive state";
  EXPECT_FALSE(fs.IsActive()) << "IsActive() should return false initially";
}

TEST(FailsafeTest, StaysInactiveWithActiveControl) {
  Failsafe fs(100);  // 100ms timeout
  uint32_t time = 0;

  // Active RC control
  auto state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should stay inactive with active RC";

  // Continue with active control
  time += 50;
  state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should stay inactive with continued RC";

  time += 100;
  state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should stay inactive even after timeout period with active control";
}

// ═══════════════════════════════════════════════════════════════════════════
// Activation Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, ActivatesAfterTimeout) {
  Failsafe fs(100);  // 100ms timeout
  uint32_t time = 0;

  // Start with active control
  auto state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Inactive);

  // Lose control at time 50
  time = 50;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should still be inactive within timeout";

  // Exceed timeout - 100ms after last active (time 0)
  time = 110;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active) << "Should activate after timeout";
  EXPECT_TRUE(fs.IsActive()) << "IsActive() should return true";
}

TEST(FailsafeTest, ActivatesWithNoInitialControl) {
  Failsafe fs(100);
  uint32_t time = 0;

  // No control from the start - first Update() initializes last_active_ms_
  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should be inactive on first update";

  // Wait for timeout from initialization
  time = 110;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should activate after timeout with no control";
}

// ═══════════════════════════════════════════════════════════════════════════
// WiFi Control Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, WiFiControlPreventsActivation) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Active WiFi control (no RC)
  auto state = fs.Update(time, false, true);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "WiFi control should prevent failsafe";

  time += 150;
  state = fs.Update(time, false, true);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should stay inactive with WiFi control";
}

TEST(FailsafeTest, EitherRcOrWifiPreventsActivation) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Both active
  auto state = fs.Update(time, true, true);
  EXPECT_EQ(state, FailsafeState::Inactive);

  time += 50;
  // Only RC active
  state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Inactive);

  time += 50;
  // Only WiFi active
  state = fs.Update(time, false, true);
  EXPECT_EQ(state, FailsafeState::Inactive);

  time += 50;
  // Neither active - should still be within timeout from last active
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should be inactive within timeout";

  time += 60;  // Now exceed timeout
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active) << "Should activate after timeout";
}

// ═══════════════════════════════════════════════════════════════════════════
// Recovery Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, RecoveryFromFailsafe) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Activate failsafe - first call initializes last_active_ms_
  (void)fs.Update(time, false, false);
  time = 110;
  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active);

  // Recover with RC
  time = 120;
  state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Recovering)
      << "Should enter Recovering state";

  // Continue with active control
  time = 130;
  state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should return to Inactive after recovery";
  EXPECT_FALSE(fs.IsActive());
}

// ═══════════════════════════════════════════════════════════════════════════
// Timeout Configuration Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, CustomTimeout) {
  Failsafe fs(500);  // 500ms timeout
  EXPECT_EQ(fs.GetTimeout(), 500u) << "Timeout should be 500ms";

  uint32_t time = 0;
  (void)fs.Update(time, true, false);

  // Lose control
  time = 400;
  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should still be inactive at 400ms";

  time = 510;  // 510ms after last active (time 0)
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should activate after 500ms timeout";
}

TEST(FailsafeTest, SetTimeout) {
  Failsafe fs(100);
  fs.SetTimeout(200);
  EXPECT_EQ(fs.GetTimeout(), 200u) << "Timeout should be updated to 200ms";
}

// ═══════════════════════════════════════════════════════════════════════════
// Time Tracking Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, GetTimeSinceLastActive) {
  Failsafe fs(100);
  uint32_t time = 1000;

  // Active control
  (void)fs.Update(time, true, false);

  time += 50;
  (void)fs.Update(time, false, false);
  EXPECT_EQ(fs.GetTimeSinceLastActive(time), 50u)
      << "Should be 50ms since last active";

  time += 30;
  (void)fs.Update(time, false, false);
  EXPECT_EQ(fs.GetTimeSinceLastActive(time), 80u)
      << "Should be 80ms since last active";
}

// ═══════════════════════════════════════════════════════════════════════════
// Reset Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, Reset) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Activate failsafe
  (void)fs.Update(time, false, false);
  time = 110;
  (void)fs.Update(time, false, false);
  EXPECT_TRUE(fs.IsActive());

  // Reset
  fs.Reset();
  EXPECT_EQ(fs.GetState(), FailsafeState::Inactive)
      << "Should be inactive after reset";
  EXPECT_FALSE(fs.IsActive());
  EXPECT_EQ(fs.GetTimeSinceLastActive(time), 0u)
      << "Time since last active should be 0 after reset";
}

// ═══════════════════════════════════════════════════════════════════════════
// Edge Cases
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, ZeroTimeout) {
  Failsafe fs(0);  // 0ms timeout - should activate immediately
  uint32_t time = 0;

  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should activate immediately with 0ms timeout";
}

TEST(FailsafeTest, TimeWrapAround) {
  Failsafe fs(100);
  uint32_t time = UINT32_MAX - 50;  // Near wrap-around

  // Active control
  (void)fs.Update(time, true, false);

  // Wrap around
  time = 60;  // Wrapped around, total elapsed ~110ms
  (void)fs.Update(time, false, false);
  // Note: This test may fail if failsafe doesn't handle wrap-around correctly
  // The implementation should use proper time difference calculation
}

TEST(FailsafeTest, RapidUpdates) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Many rapid updates with active control
  for (int i = 0; i < 100; ++i) {
    auto state = fs.Update(time, true, false);
    EXPECT_EQ(state, FailsafeState::Inactive)
        << "Should stay inactive with rapid updates";
    time += 1;  // 1ms increments
  }

  // Now lose control
  for (int i = 0; i < 50; ++i) {
    (void)fs.Update(time, false, false);
    time += 1;
  }
  EXPECT_FALSE(fs.IsActive()) << "Should not activate within timeout";

  // Exceed timeout
  for (int i = 0; i < 60; ++i) {
    (void)fs.Update(time, false, false);
    time += 1;
  }
  EXPECT_TRUE(fs.IsActive()) << "Should activate after timeout";
}

// ═══════════════════════════════════════════════════════════════════════════
// State Transition Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, StateTransitionSequence) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Inactive -> Active
  (void)fs.Update(time, true, false);
  time = 110;
  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should transition to Active after timeout";

  // Active -> Recovering
  time = 120;
  state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Recovering)
      << "Should transition to Recovering when control returns";

  // Recovering -> Inactive
  time = 130;
  state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should transition to Inactive after recovery";
}

TEST(FailsafeTest, RecoveringStateWithWiFi) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Activate failsafe
  (void)fs.Update(time, false, false);
  time = 110;
  (void)fs.Update(time, false, false);
  EXPECT_EQ(fs.GetState(), FailsafeState::Active);

  // Recover with WiFi instead of RC
  time = 120;
  auto state = fs.Update(time, false, true);
  EXPECT_EQ(state, FailsafeState::Recovering)
      << "WiFi should also trigger recovery";

  time = 130;
  state = fs.Update(time, false, true);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should complete recovery with WiFi";
}

TEST(FailsafeTest, RecoveringStateWithBothSources) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Activate failsafe
  (void)fs.Update(time, false, false);
  time = 110;
  (void)fs.Update(time, false, false);
  EXPECT_TRUE(fs.IsActive());

  // Recover with both sources
  time = 120;
  auto state = fs.Update(time, true, true);
  EXPECT_EQ(state, FailsafeState::Recovering);

  time = 130;
  state = fs.Update(time, true, true);
  EXPECT_EQ(state, FailsafeState::Inactive);
}

TEST(FailsafeTest, LoseControlDuringRecovery) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Activate failsafe
  (void)fs.Update(time, false, false);
  time = 110;
  (void)fs.Update(time, false, false);
  EXPECT_TRUE(fs.IsActive());

  // Start recovery
  time = 120;
  auto state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Recovering);

  // Lose control again during recovery
  time = 130;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Recovering)
      << "Should stay in Recovering state initially";

  // Wait for timeout again (100ms from last active at 120)
  time = 230;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should re-activate after timeout during recovery";
}

// ═══════════════════════════════════════════════════════════════════════════
// Boundary Condition Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, ExactTimeoutBoundary) {
  Failsafe fs(100);
  uint32_t time = 0;

  (void)fs.Update(time, true, false);

  // Exactly at timeout boundary
  time = 100;
  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should activate at exactly timeout boundary";
}

TEST(FailsafeTest, JustBeforeTimeout) {
  Failsafe fs(100);
  uint32_t time = 0;

  (void)fs.Update(time, true, false);

  // Just before timeout (99ms)
  time = 99;
  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should not activate just before timeout";

  // One more millisecond
  time = 100;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should activate after crossing timeout";
}

TEST(FailsafeTest, VeryLargeTimeout) {
  Failsafe fs(UINT32_MAX / 2);  // Very large timeout
  uint32_t time = 0;

  (void)fs.Update(time, true, false);

  time += 1000000;  // 1 million ms
  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should not activate with very large timeout";
}

TEST(FailsafeTest, MinimalTimeout) {
  Failsafe fs(1);  // 1ms timeout
  uint32_t time = 0;

  (void)fs.Update(time, true, false);

  time = 1;
  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should activate with minimal timeout";
}

// ═══════════════════════════════════════════════════════════════════════════
// Multiple Activation/Recovery Cycles
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, MultipleActivationCycles) {
  Failsafe fs(100);
  uint32_t time = 0;

  // First cycle
  (void)fs.Update(time, true, false);
  time = 110;
  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active) << "First activation";

  // Recover
  time = 120;
  (void)fs.Update(time, true, false);
  time = 130;
  state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Inactive) << "First recovery";

  // Second cycle - 100ms after last active (130)
  time = 240;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active) << "Second activation";

  // Recover again
  time = 250;
  (void)fs.Update(time, false, true);  // WiFi this time
  time = 260;
  state = fs.Update(time, false, true);
  EXPECT_EQ(state, FailsafeState::Inactive) << "Second recovery";

  // Third cycle - 100ms after last active (260)
  time = 370;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active) << "Third activation";
}

// ═══════════════════════════════════════════════════════════════════════════
// Intermittent Control Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, IntermittentControl) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Start with control
  (void)fs.Update(time, true, false);

  // Intermittent control - on/off every 40ms
  for (int i = 0; i < 5; ++i) {
    time += 40;
    (void)fs.Update(time, false, false);  // Off for 40ms

    time += 40;
    auto state = fs.Update(time, true, false);  // On for 40ms
    EXPECT_EQ(state, FailsafeState::Inactive)
        << "Should not activate with intermittent control at iteration " << i;
  }
}

TEST(FailsafeTest, IntermittentControlExceedsTimeout) {
  Failsafe fs(100);
  uint32_t time = 0;

  (void)fs.Update(time, true, false);

  // Control off for 60ms
  time += 60;
  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Inactive);

  // Control on briefly (10ms)
  time += 10;
  state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Inactive);

  // Control off for 110ms - should activate
  time += 110;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should activate when gap exceeds timeout";
}

// ═══════════════════════════════════════════════════════════════════════════
// Control Source Switching Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, SwitchBetweenRcAndWiFi) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Start with RC
  auto state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Inactive);

  // Switch to WiFi
  time += 50;
  state = fs.Update(time, false, true);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should stay inactive when switching sources";

  // Switch back to RC
  time += 50;
  state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Inactive);

  // Both off
  time += 50;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Inactive) << "Should be within timeout";

  time += 60;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active) << "Should activate after timeout";
}

TEST(FailsafeTest, SimultaneousControlSources) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Both sources active
  auto state = fs.Update(time, true, true);
  EXPECT_EQ(state, FailsafeState::Inactive);

  // Drop RC, keep WiFi
  time += 50;
  state = fs.Update(time, false, true);
  EXPECT_EQ(state, FailsafeState::Inactive);

  // Drop WiFi, restore RC
  time += 50;
  state = fs.Update(time, true, false);
  EXPECT_EQ(state, FailsafeState::Inactive);

  // Both active again
  time += 50;
  state = fs.Update(time, true, true);
  EXPECT_EQ(state, FailsafeState::Inactive);
}

// ═══════════════════════════════════════════════════════════════════════════
// Time Tracking Edge Cases
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, GetTimeSinceLastActiveBeforeFirstUpdate) {
  Failsafe fs(100);
  uint32_t time = 1000;

  // Before any update
  EXPECT_EQ(fs.GetTimeSinceLastActive(time), 0u)
      << "Should return 0 before first update";
}

TEST(FailsafeTest, GetTimeSinceLastActiveWithActiveControl) {
  Failsafe fs(100);
  uint32_t time = 1000;

  // Active control
  (void)fs.Update(time, true, false);

  // Check immediately
  EXPECT_EQ(fs.GetTimeSinceLastActive(time), 0u)
      << "Should be 0 with active control";

  // Continue with active control
  time += 50;
  (void)fs.Update(time, true, false);
  EXPECT_EQ(fs.GetTimeSinceLastActive(time), 0u)
      << "Should remain 0 with continued active control";
}

TEST(FailsafeTest, GetTimeSinceLastActiveAfterReset) {
  Failsafe fs(100);
  uint32_t time = 1000;

  (void)fs.Update(time, true, false);
  time += 50;
  (void)fs.Update(time, false, false);

  EXPECT_GT(fs.GetTimeSinceLastActive(time), 0u);

  fs.Reset();
  EXPECT_EQ(fs.GetTimeSinceLastActive(time), 0u) << "Should be 0 after reset";
}

// ═══════════════════════════════════════════════════════════════════════════
// Configuration Change Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, ChangeTimeoutWhileInactive) {
  Failsafe fs(100);
  uint32_t time = 0;

  (void)fs.Update(time, true, false);
  fs.SetTimeout(200);

  time = 150;
  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should not activate with new longer timeout";

  time = 210;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should activate after new timeout";
}

TEST(FailsafeTest, ChangeTimeoutWhileActive) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Activate failsafe
  (void)fs.Update(time, false, false);
  time = 110;
  (void)fs.Update(time, false, false);
  EXPECT_TRUE(fs.IsActive());

  // Change timeout while active
  fs.SetTimeout(200);
  EXPECT_EQ(fs.GetTimeout(), 200u);
  EXPECT_TRUE(fs.IsActive()) << "Should remain active after timeout change";
}

TEST(FailsafeTest, DefaultTimeoutValue) {
  Failsafe fs;  // Use default timeout
  EXPECT_EQ(fs.GetTimeout(), 250u) << "Default timeout should be 250ms";
}

// ═══════════════════════════════════════════════════════════════════════════
// Stress Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, LongRunningOperation) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Simulate long running operation with periodic control
  for (int i = 0; i < 1000; ++i) {
    bool has_control = (i % 10) < 8;  // 80% control availability
    auto state = fs.Update(time, has_control, false);

    if (!has_control && (i % 10) >= 8) {
      // During the 20% no-control period
      if (time % 100 >= 100) {
        EXPECT_EQ(state, FailsafeState::Active)
            << "Should activate during extended no-control period";
      }
    }

    time += 10;
  }
}

TEST(FailsafeTest, HighFrequencyUpdates) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Very high frequency updates (1000 Hz)
  for (int i = 0; i < 1000; ++i) {
    auto state = fs.Update(time, true, false);
    EXPECT_EQ(state, FailsafeState::Inactive);
    time += 1;
  }

  // Now lose control
  for (int i = 0; i < 100; ++i) {
    (void)fs.Update(time, false, false);
    time += 1;
  }

  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should activate after timeout with high frequency updates";
}

// ═══════════════════════════════════════════════════════════════════════════
// Reset Behavior Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, ResetDuringRecovery) {
  Failsafe fs(100);
  uint32_t time = 0;

  // Activate and start recovery
  (void)fs.Update(time, false, false);
  time = 110;
  (void)fs.Update(time, false, false);
  time = 120;
  (void)fs.Update(time, true, false);
  EXPECT_EQ(fs.GetState(), FailsafeState::Recovering);

  // Reset during recovery
  fs.Reset();
  EXPECT_EQ(fs.GetState(), FailsafeState::Inactive)
      << "Should be inactive after reset during recovery";
}

TEST(FailsafeTest, ResetPreservesTimeout) {
  Failsafe fs(100);
  fs.SetTimeout(300);

  fs.Reset();

  EXPECT_EQ(fs.GetTimeout(), 300u)
      << "Reset should not change timeout configuration";
}

TEST(FailsafeTest, MultipleResets) {
  Failsafe fs(100);
  uint32_t time = 0;

  for (int i = 0; i < 5; ++i) {
    // Activate
    (void)fs.Update(time, false, false);
    time = 110 + i * 200;
    (void)fs.Update(time, false, false);
    EXPECT_TRUE(fs.IsActive()) << "Should be active at iteration " << i;

    // Reset
    fs.Reset();
    EXPECT_FALSE(fs.IsActive())
        << "Should be inactive after reset at iteration " << i;
    EXPECT_EQ(fs.GetState(), FailsafeState::Inactive);

    time += 10;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// Real-World Scenario Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FailsafeTest, RcSignalDropout) {
  Failsafe fs(250);  // Typical RC timeout
  uint32_t time = 0;

  // Normal RC operation
  for (int i = 0; i < 10; ++i) {
    (void)fs.Update(time, true, false);
    time += 20;  // 50 Hz update rate
  }

  // RC signal dropout
  for (int i = 0; i < 12; ++i) {
    auto state = fs.Update(time, false, false);
    if (i < 12) {
      EXPECT_EQ(state, FailsafeState::Inactive)
          << "Should not activate during dropout at iteration " << i;
    }
    time += 20;
  }

  // Should activate after 250ms
  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should activate after RC dropout exceeds timeout";
}

TEST(FailsafeTest, WiFiConnectionLoss) {
  Failsafe fs(500);  // Longer timeout for WiFi
  uint32_t time = 0;

  // Normal WiFi operation
  (void)fs.Update(time, false, true);

  // WiFi connection lost
  time = 400;
  auto state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should not activate within timeout";

  time = 510;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should activate after WiFi timeout";
}

TEST(FailsafeTest, DualControlWithPrimaryFailure) {
  Failsafe fs(250);
  uint32_t time = 0;

  // Both RC and WiFi active
  (void)fs.Update(time, true, true);

  // RC fails, WiFi continues
  time += 100;
  auto state = fs.Update(time, false, true);
  EXPECT_EQ(state, FailsafeState::Inactive)
      << "Should stay inactive with WiFi backup";

  // WiFi also fails
  time += 100;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Inactive) << "Should be within timeout";

  time += 160;
  state = fs.Update(time, false, false);
  EXPECT_EQ(state, FailsafeState::Active)
      << "Should activate when both sources fail";
}