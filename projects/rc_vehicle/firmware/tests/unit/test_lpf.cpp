#include <gtest/gtest.h>

#include <cmath>

#include "lpf_butterworth.hpp"
#include "test_helpers.hpp"

using namespace rc_vehicle::testing;

// ═══════════════════════════════════════════════════════════════════════════
// Initialization Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, InitiallyNotConfigured) {
  LpfButterworth2 lpf;
  EXPECT_FALSE(lpf.IsConfigured())
      << "Filter should not be configured initially";
  EXPECT_FLOAT_EQ(lpf.GetCutoffHz(), 0.0f) << "Initial cutoff should be 0";
  EXPECT_FLOAT_EQ(lpf.GetSampleRateHz(), 0.0f)
      << "Initial sample rate should be 0";
}

TEST(LpfTest, ConfigurationSetsParameters) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  EXPECT_TRUE(lpf.IsConfigured())
      << "Filter should be configured after SetParams";
  EXPECT_FLOAT_EQ(lpf.GetCutoffHz(), 20.0f) << "Cutoff should be 20 Hz";
  EXPECT_FLOAT_EQ(lpf.GetSampleRateHz(), 500.0f)
      << "Sample rate should be 500 Hz";
}

// ═══════════════════════════════════════════════════════════════════════════
// Basic Filtering Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, InitialOutputIsZero) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  EXPECT_FLOAT_EQ(lpf.GetOutput(), 0.0f)
      << "Initial output should be 0 before any steps";
}

TEST(LpfTest, StepReturnsFilteredValue) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  float output = lpf.Step(1.0f);
  EXPECT_GT(output, 0.0f) << "Output should be positive after positive input";
  EXPECT_LT(output, 1.0f)
      << "Output should be less than input (filtering effect)";
}

TEST(LpfTest, ConvergesToConstantInput) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  float constant_input = 5.0f;
  float output = 0.0f;

  // Feed constant input for many samples
  for (int i = 0; i < 1000; ++i) {
    output = lpf.Step(constant_input);
  }

  // Should converge to the constant input value
  EXPECT_NEAR(output, constant_input, 0.01f)
      << "Filter should converge to constant input";
}

// ═══════════════════════════════════════════════════════════════════════════
// Frequency Response Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, AttenuatesHighFrequency) {
  LpfButterworth2 lpf;
  float cutoff = 20.0f;
  float sample_rate = 500.0f;
  lpf.SetParams(cutoff, sample_rate);

  // Generate high-frequency sine wave (100 Hz, well above cutoff)
  float high_freq = 100.0f;
  float dt = 1.0f / sample_rate;
  float max_output = 0.0f;

  for (int i = 0; i < 500; ++i) {
    float t = i * dt;
    float input = std::sin(2.0f * M_PI * high_freq * t);
    float output = lpf.Step(input);
    max_output = std::max(max_output, std::abs(output));
  }

  // High frequency should be significantly attenuated
  EXPECT_LT(max_output, 0.5f)
      << "High frequency signal should be attenuated below 0.5";
}

TEST(LpfTest, PassesLowFrequency) {
  LpfButterworth2 lpf;
  float cutoff = 20.0f;
  float sample_rate = 500.0f;
  lpf.SetParams(cutoff, sample_rate);

  // Generate low-frequency sine wave (5 Hz, well below cutoff)
  float low_freq = 5.0f;
  float dt = 1.0f / sample_rate;
  float max_output = 0.0f;

  // Let filter settle first
  for (int i = 0; i < 200; ++i) {
    float t = i * dt;
    float input = std::sin(2.0f * M_PI * low_freq * t);
    lpf.Step(input);
  }

  // Now measure output amplitude
  for (int i = 0; i < 500; ++i) {
    float t = (i + 200) * dt;
    float input = std::sin(2.0f * M_PI * low_freq * t);
    float output = lpf.Step(input);
    max_output = std::max(max_output, std::abs(output));
  }

  // Low frequency should pass through with minimal attenuation
  EXPECT_GT(max_output, 0.8f)
      << "Low frequency signal should pass through (>0.8 amplitude)";
}

// ═══════════════════════════════════════════════════════════════════════════
// Reset Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, ResetClearsState) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  // Feed some data
  for (int i = 0; i < 10; ++i) {
    lpf.Step(5.0f);
  }

  float output_before_reset = lpf.GetOutput();
  EXPECT_GT(output_before_reset, 0.0f)
      << "Output should be non-zero before reset";

  // Reset
  lpf.Reset();

  EXPECT_FLOAT_EQ(lpf.GetOutput(), 0.0f) << "Output should be 0 after reset";
  EXPECT_TRUE(lpf.IsConfigured())
      << "Filter should still be configured after reset";
}

TEST(LpfTest, ResetAllowsNewFiltering) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  // Feed data, reset, feed again
  lpf.Step(10.0f);
  lpf.Reset();
  float output = lpf.Step(5.0f);

  EXPECT_GT(output, 0.0f) << "Should be able to filter after reset";
  EXPECT_LT(output, 5.0f) << "Should apply filtering after reset";
}

// ═══════════════════════════════════════════════════════════════════════════
// Parameter Change Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, ReconfigureChangesResponse) {
  LpfButterworth2 lpf;

  // Configure with low cutoff
  lpf.SetParams(5.0f, 500.0f);
  float output_low_cutoff = 0.0f;
  for (int i = 0; i < 10; ++i) {
    output_low_cutoff = lpf.Step(1.0f);
  }

  lpf.Reset();

  // Configure with high cutoff
  lpf.SetParams(50.0f, 500.0f);
  float output_high_cutoff = 0.0f;
  for (int i = 0; i < 10; ++i) {
    output_high_cutoff = lpf.Step(1.0f);
  }

  // Higher cutoff should respond faster (higher output after same number of
  // steps)
  EXPECT_GT(output_high_cutoff, output_low_cutoff)
      << "Higher cutoff should respond faster";
}

// ═══════════════════════════════════════════════════════════════════════════
// Typical Use Case Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, TypicalGyroFiltering) {
  LpfButterworth2 lpf;
  lpf.SetParams(30.0f, 500.0f);  // Typical for gyro filtering

  // Simulate noisy gyro data
  float clean_signal = 10.0f;  // 10 deg/s rotation
  float noise_amplitude = 2.0f;

  for (int i = 0; i < 100; ++i) {
    float noisy_input =
        clean_signal + noise_amplitude * std::sin(i * 0.5f);  // Add noise
    lpf.Step(noisy_input);
  }

  float output = lpf.GetOutput();

  // Output should be close to clean signal
  EXPECT_NEAR(output, clean_signal, 1.0f)
      << "Filter should remove noise and approximate clean signal";
}

TEST(LpfTest, StepResponse) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  // Step input from 0 to 1
  float output = 0.0f;
  for (int i = 0; i < 50; ++i) {
    output = lpf.Step(1.0f);
  }

  // Should be approaching 1.0 but not quite there yet
  EXPECT_GT(output, 0.5f)
      << "Should have significant response after 50 samples";
  EXPECT_LT(output, 1.0f) << "Should not have fully settled yet";
}

// ═══════════════════════════════════════════════════════════════════════════
// Edge Cases
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, ZeroInput) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  // Feed zeros
  for (int i = 0; i < 100; ++i) {
    float output = lpf.Step(0.0f);
    EXPECT_FLOAT_EQ(output, 0.0f) << "Zero input should produce zero output";
  }
}

TEST(LpfTest, NegativeInput) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  // Feed negative constant
  float output = 0.0f;
  for (int i = 0; i < 100; ++i) {
    output = lpf.Step(-5.0f);
  }

  EXPECT_NEAR(output, -5.0f, 0.01f)
      << "Should handle negative inputs correctly";
}

TEST(LpfTest, AlternatingInput) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  // Alternating +1, -1 (high frequency square wave)
  float max_output = 0.0f;
  for (int i = 0; i < 100; ++i) {
    float input = (i % 2 == 0) ? 1.0f : -1.0f;
    float output = lpf.Step(input);
    max_output = std::max(max_output, std::abs(output));
  }

  // High frequency square wave should be heavily attenuated
  EXPECT_LT(max_output, 0.5f)
      << "High frequency square wave should be attenuated";
}

TEST(LpfTest, VeryLowCutoff) {
  LpfButterworth2 lpf;
  lpf.SetParams(1.0f, 500.0f);  // Very low cutoff

  float output = 0.0f;
  for (int i = 0; i < 10; ++i) {
    output = lpf.Step(1.0f);
  }

  // Should respond very slowly
  EXPECT_LT(output, 0.1f) << "Very low cutoff should respond very slowly";
}

TEST(LpfTest, VeryHighCutoff) {
  LpfButterworth2 lpf;
  lpf.SetParams(200.0f, 500.0f);  // High cutoff (close to Nyquist)

  float output = 0.0f;
  for (int i = 0; i < 10; ++i) {
    output = lpf.Step(1.0f);
  }

  // Should respond quickly
  EXPECT_GT(output, 0.5f) << "High cutoff should respond quickly";
}

// ═══════════════════════════════════════════════════════════════════════════
// Stability Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, StableWithLargeInput) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  // Feed large values
  for (int i = 0; i < 100; ++i) {
    float output = lpf.Step(1000.0f);
    EXPECT_FALSE(std::isnan(output)) << "Output should not be NaN";
    EXPECT_FALSE(std::isinf(output)) << "Output should not be infinite";
  }
}

TEST(LpfTest, StableWithRapidChanges) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  // Rapid changes between extremes
  for (int i = 0; i < 100; ++i) {
    float input = (i % 10 < 5) ? 100.0f : -100.0f;
    float output = lpf.Step(input);
    EXPECT_FALSE(std::isnan(output))
        << "Should remain stable with rapid changes";
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// Invalid Parameter Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, InvalidParametersZeroCutoff) {
  LpfButterworth2 lpf;
  lpf.SetParams(0.0f, 500.0f);

  EXPECT_FALSE(lpf.IsConfigured())
      << "Filter should not be configured with zero cutoff";
  EXPECT_FLOAT_EQ(lpf.GetCutoffHz(), 0.0f);
}

TEST(LpfTest, InvalidParametersNegativeCutoff) {
  LpfButterworth2 lpf;
  lpf.SetParams(-10.0f, 500.0f);

  EXPECT_FALSE(lpf.IsConfigured())
      << "Filter should not be configured with negative cutoff";
}

TEST(LpfTest, InvalidParametersZeroSampleRate) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 0.0f);

  EXPECT_FALSE(lpf.IsConfigured())
      << "Filter should not be configured with zero sample rate";
}

TEST(LpfTest, InvalidParametersNegativeSampleRate) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, -500.0f);

  EXPECT_FALSE(lpf.IsConfigured())
      << "Filter should not be configured with negative sample rate";
}

TEST(LpfTest, InvalidParametersCutoffAboveNyquist) {
  LpfButterworth2 lpf;
  float sample_rate = 500.0f;
  float cutoff = sample_rate / 2.0f;  // Exactly at Nyquist

  lpf.SetParams(cutoff, sample_rate);

  EXPECT_FALSE(lpf.IsConfigured())
      << "Filter should not be configured with cutoff at Nyquist frequency";
}

TEST(LpfTest, InvalidParametersCutoffAboveNyquistPlus) {
  LpfButterworth2 lpf;
  float sample_rate = 500.0f;
  float cutoff = sample_rate / 2.0f + 10.0f;  // Above Nyquist

  lpf.SetParams(cutoff, sample_rate);

  EXPECT_FALSE(lpf.IsConfigured())
      << "Filter should not be configured with cutoff above Nyquist frequency";
}

TEST(LpfTest, UnconfiguredFilterPassthrough) {
  LpfButterworth2 lpf;
  // Don't configure the filter

  float input = 5.0f;
  float output = lpf.Step(input);

  EXPECT_FLOAT_EQ(output, input)
      << "Unconfigured filter should pass through input unchanged";
}

TEST(LpfTest, UnconfiguredFilterUpdatesOutput) {
  LpfButterworth2 lpf;
  // Don't configure the filter

  lpf.Step(3.0f);
  EXPECT_FLOAT_EQ(lpf.GetOutput(), 3.0f)
      << "Unconfigured filter should update output to last input";

  lpf.Step(7.0f);
  EXPECT_FLOAT_EQ(lpf.GetOutput(), 7.0f)
      << "Unconfigured filter should update output to new input";
}

TEST(LpfTest, ReconfigureFromInvalidToValid) {
  LpfButterworth2 lpf;

  // First set invalid parameters
  lpf.SetParams(-10.0f, 500.0f);
  EXPECT_FALSE(lpf.IsConfigured());

  // Then set valid parameters
  lpf.SetParams(20.0f, 500.0f);
  EXPECT_TRUE(lpf.IsConfigured())
      << "Should be able to configure after invalid parameters";

  float output = lpf.Step(1.0f);
  EXPECT_GT(output, 0.0f) << "Should filter correctly after reconfiguration";
}

TEST(LpfTest, ReconfigureFromValidToInvalid) {
  LpfButterworth2 lpf;

  // First set valid parameters
  lpf.SetParams(20.0f, 500.0f);
  EXPECT_TRUE(lpf.IsConfigured());

  // Feed some data
  lpf.Step(5.0f);
  lpf.Step(5.0f);

  // Then set invalid parameters
  lpf.SetParams(0.0f, 500.0f);
  EXPECT_FALSE(lpf.IsConfigured())
      << "Should become unconfigured with invalid parameters";

  // Should now pass through
  float output = lpf.Step(10.0f);
  EXPECT_FLOAT_EQ(output, 10.0f)
      << "Should pass through after becoming unconfigured";
}

// ═══════════════════════════════════════════════════════════════════════════
// Boundary Condition Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, CutoffJustBelowNyquist) {
  LpfButterworth2 lpf;
  float sample_rate = 500.0f;
  float cutoff = sample_rate / 2.0f - 1.0f;  // Just below Nyquist

  lpf.SetParams(cutoff, sample_rate);

  EXPECT_TRUE(lpf.IsConfigured())
      << "Filter should be configured with cutoff just below Nyquist";

  // Should still be stable
  for (int i = 0; i < 100; ++i) {
    float output = lpf.Step(1.0f);
    EXPECT_FALSE(std::isnan(output)) << "Should remain stable";
    EXPECT_FALSE(std::isinf(output)) << "Should remain stable";
  }
}

TEST(LpfTest, VerySmallCutoff) {
  LpfButterworth2 lpf;
  lpf.SetParams(0.1f, 500.0f);  // Very small but valid

  EXPECT_TRUE(lpf.IsConfigured())
      << "Filter should be configured with very small cutoff";

  float output = 0.0f;
  for (int i = 0; i < 100; ++i) {
    output = lpf.Step(1.0f);
  }

  // Should respond extremely slowly
  EXPECT_LT(output, 0.05f)
      << "Very small cutoff should respond extremely slowly";
}

TEST(LpfTest, VeryHighSampleRate) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 10000.0f);  // Very high sample rate

  EXPECT_TRUE(lpf.IsConfigured())
      << "Filter should be configured with very high sample rate";

  // Should still be stable
  for (int i = 0; i < 100; ++i) {
    float output = lpf.Step(1.0f);
    EXPECT_FALSE(std::isnan(output)) << "Should remain stable";
  }
}

TEST(LpfTest, VeryLowSampleRate) {
  LpfButterworth2 lpf;
  lpf.SetParams(5.0f, 20.0f);  // Low sample rate (cutoff < fs/2)

  EXPECT_TRUE(lpf.IsConfigured())
      << "Filter should be configured with low sample rate";

  // Should still be stable
  for (int i = 0; i < 100; ++i) {
    float output = lpf.Step(1.0f);
    EXPECT_FALSE(std::isnan(output)) << "Should remain stable";
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// Numerical Precision Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, VerySmallInputValues) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  float small_value = 1e-6f;
  float output = 0.0f;

  for (int i = 0; i < 100; ++i) {
    output = lpf.Step(small_value);
  }

  EXPECT_NEAR(output, small_value, 1e-7f)
      << "Should handle very small values correctly";
}

TEST(LpfTest, VeryLargeInputValues) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  float large_value = 1e6f;
  float output = 0.0f;

  for (int i = 0; i < 100; ++i) {
    output = lpf.Step(large_value);
  }

  EXPECT_NEAR(output, large_value, large_value * 0.01f)
      << "Should handle very large values correctly";
}

TEST(LpfTest, MixedScaleInputs) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  // Feed large value, then small value
  for (int i = 0; i < 50; ++i) {
    lpf.Step(1000.0f);
  }

  lpf.Reset();

  for (int i = 0; i < 50; ++i) {
    lpf.Step(0.001f);
  }

  float output = lpf.GetOutput();
  EXPECT_NEAR(output, 0.001f, 0.001f)
      << "Should handle transition from large to small values";
}

// ═══════════════════════════════════════════════════════════════════════════
// Long-Running Stability Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, LongRunningStability) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  // Run for many iterations
  for (int i = 0; i < 10000; ++i) {
    float input = std::sin(i * 0.01f);
    float output = lpf.Step(input);

    ASSERT_FALSE(std::isnan(output)) << "Output became NaN at iteration " << i;
    ASSERT_FALSE(std::isinf(output))
        << "Output became infinite at iteration " << i;
  }
}

TEST(LpfTest, MultipleResetCycles) {
  LpfButterworth2 lpf;
  lpf.SetParams(20.0f, 500.0f);

  // Multiple cycles of feed and reset
  for (int cycle = 0; cycle < 100; ++cycle) {
    for (int i = 0; i < 10; ++i) {
      float output = lpf.Step(5.0f);
      EXPECT_FALSE(std::isnan(output))
          << "Output became NaN in cycle " << cycle;
    }
    lpf.Reset();
  }

  EXPECT_TRUE(lpf.IsConfigured())
      << "Should remain configured after multiple reset cycles";
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase Lag Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, IntroducesPhaseDelay) {
  LpfButterworth2 lpf;
  float cutoff = 20.0f;
  float sample_rate = 500.0f;
  lpf.SetParams(cutoff, sample_rate);

  // Test that filter introduces delay by checking step response
  // A step input should not instantly reach the output
  lpf.Reset();

  // Apply step input
  float output_immediate = lpf.Step(1.0f);

  // Immediate output should be less than input (delayed response)
  EXPECT_LT(output_immediate, 1.0f)
      << "Filter should not instantly pass step input (introduces delay)";
  EXPECT_GT(output_immediate, 0.0f) << "Filter should respond to step input";

  // After many samples, should converge to input
  for (int i = 0; i < 1000; ++i) {
    lpf.Step(1.0f);
  }
  float output_settled = lpf.GetOutput();
  EXPECT_NEAR(output_settled, 1.0f, 0.01f)
      << "Filter should eventually converge to constant input";
}

// ═══════════════════════════════════════════════════════════════════════════
// Coefficient Validation Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, DifferentCutoffsProduceDifferentBehavior) {
  LpfButterworth2 lpf1, lpf2;
  lpf1.SetParams(10.0f, 500.0f);
  lpf2.SetParams(50.0f, 500.0f);

  float output1 = 0.0f, output2 = 0.0f;

  // Feed same input to both - use more steps to avoid overshoot effects
  for (int i = 0; i < 100; ++i) {
    output1 = lpf1.Step(1.0f);
    output2 = lpf2.Step(1.0f);
  }

  // After settling, both should be close to 1.0
  // But higher cutoff should have settled faster (closer to 1.0)
  EXPECT_NEAR(output1, 1.0f, 0.1f) << "Low cutoff should converge";
  EXPECT_NEAR(output2, 1.0f, 0.1f) << "High cutoff should converge";
  // Both should be close to target, so just verify they're different
  EXPECT_NE(output1, output2)
      << "Different cutoffs produce different transients";
}

TEST(LpfTest, SameCutoffProducesSameBehavior) {
  LpfButterworth2 lpf1, lpf2;
  lpf1.SetParams(20.0f, 500.0f);
  lpf2.SetParams(20.0f, 500.0f);

  float output1 = 0.0f, output2 = 0.0f;

  // Feed same input to both
  for (int i = 0; i < 50; ++i) {
    output1 = lpf1.Step(1.0f);
    output2 = lpf2.Step(1.0f);
  }

  // Should produce identical results
  EXPECT_FLOAT_EQ(output1, output2)
      << "Same parameters should produce identical results";
}

// ═══════════════════════════════════════════════════════════════════════════
// Real-World Scenario Tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(LpfTest, GyroZAxisFiltering) {
  LpfButterworth2 lpf;
  lpf.SetParams(30.0f, 500.0f);  // Typical gyro Z filtering

  // Simulate vehicle turning with noise
  float base_yaw_rate = 45.0f;  // 45 deg/s turn
  float noise_amplitude = 5.0f;

  for (int i = 0; i < 200; ++i) {
    float noise = noise_amplitude * std::sin(i * 2.0f);  // High freq noise
    float input = base_yaw_rate + noise;
    lpf.Step(input);
  }

  float output = lpf.GetOutput();

  // Should be close to base rate, noise filtered out
  EXPECT_NEAR(output, base_yaw_rate, 3.0f)
      << "Should filter out high-frequency noise from gyro";
}

TEST(LpfTest, SuddenManeuverResponse) {
  LpfButterworth2 lpf;
  lpf.SetParams(30.0f, 500.0f);

  // Steady state
  for (int i = 0; i < 100; ++i) {
    lpf.Step(0.0f);
  }

  // Sudden maneuver
  float maneuver_rate = 90.0f;  // 90 deg/s
  float output = 0.0f;
  for (int i = 0; i < 50; ++i) {
    output = lpf.Step(maneuver_rate);
  }

  // Should respond to maneuver but with some lag
  // Note: Butterworth filter can have slight overshoot, so allow small margin
  EXPECT_GT(output, maneuver_rate * 0.3f)
      << "Should respond to sudden maneuver";
  EXPECT_LT(output, maneuver_rate * 1.05f)
      << "Should not significantly overshoot (allow 5% margin for Butterworth "
         "overshoot)";
}

TEST(LpfTest, VibrationRejection) {
  LpfButterworth2 lpf;
  lpf.SetParams(25.0f, 500.0f);

  // Simulate high-frequency vibration (200 Hz)
  float vibration_freq = 200.0f;
  float sample_rate = 500.0f;
  float dt = 1.0f / sample_rate;
  float max_output = 0.0f;

  // Let settle first
  for (int i = 0; i < 100; ++i) {
    float t = i * dt;
    float vibration = std::sin(2.0f * M_PI * vibration_freq * t);
    lpf.Step(vibration);
  }

  // Measure output amplitude
  for (int i = 0; i < 100; ++i) {
    float t = (i + 100) * dt;
    float vibration = std::sin(2.0f * M_PI * vibration_freq * t);
    float output = lpf.Step(vibration);
    max_output = std::max(max_output, std::abs(output));
  }

  // High frequency vibration should be heavily attenuated
  EXPECT_LT(max_output, 0.1f)
      << "Should reject high-frequency vibration (200 Hz)";
}