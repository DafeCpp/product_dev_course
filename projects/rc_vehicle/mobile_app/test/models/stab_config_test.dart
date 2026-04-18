import 'package:flutter_test/flutter_test.dart';
import 'package:rc_vehicle_app/models/stab_config.dart';

void main() {
  group('StabConfig', () {
    test('fromJson parses full config', () {
      final json = {
        'type': 'stab_config',
        'enabled': true,
        'mode': 3,
        'fade_ms': 250,
        'yaw_rate': {
          'pid': {'kp': 0.15, 'ki': 0.01, 'kd': 0.008, 'max_integral': 0.3, 'max_correction': 0.25},
          'steer_to_yaw_rate_dps': 120.0,
        },
        'slip_angle': {
          'pid': {'kp': 0.2, 'ki': 0.0, 'kd': 0.01, 'max_integral': 0.5, 'max_correction': 0.3},
          'target_deg': 5.0,
        },
        'adaptive': {'enabled': true, 'speed_ref_ms': 2.0, 'scale_min': 0.3, 'scale_max': 3.0},
        'oversteer': {'warn_enabled': true, 'slip_thresh_deg': 15.0, 'rate_thresh_deg_s': 40.0, 'throttle_reduction': 0.5},
        'pitch_comp': {'enabled': true, 'gain': 0.02, 'max_correction': 0.3},
        'kids_mode': {
          'throttle_limit': 0.15,
          'reverse_limit': 0.1,
          'steering_limit': 0.5,
          'slew_throttle': 0.2,
          'slew_steering': 0.3,
          'anti_spin_enabled': true,
          'anti_spin_threshold_deg': 5.0,
          'anti_spin_reduction': 0.8,
          'accel_limit_enabled': true,
          'accel_threshold_g': 0.1,
          'accel_limit_gain': 5.0,
          'accel_max_reduction': 0.7,
        },
        'steering_trim': 0.05,
        'throttle_trim': -0.02,
        'filter': {
          'madgwick_beta': 0.15,
          'lpf_cutoff_hz': 25.0,
          'adaptive_beta_enabled': false,
        },
      };

      final config = StabConfig.fromJson(json);
      expect(config.enabled, true);
      expect(config.mode, DriveMode.kids);
      expect(config.fadeMs, 250);
      expect(config.yawRatePid.kp, closeTo(0.15, 0.001));
      expect(config.yawRatePid.ki, closeTo(0.01, 0.001));
      expect(config.steerToYawRateDps, closeTo(120.0, 0.1));
      expect(config.slipTargetDeg, closeTo(5.0, 0.1));
      expect(config.adaptiveEnabled, true);
      expect(config.oversteerWarnEnabled, true);
      expect(config.pitchCompEnabled, true);
      expect(config.kidsMode.throttleLimit, closeTo(0.15, 0.001));
      expect(config.kidsMode.antiSpinEnabled, true);
      expect(config.steeringTrim, closeTo(0.05, 0.001));
      expect(config.madgwickBeta, closeTo(0.15, 0.001));
      expect(config.adaptiveBetaEnabled, false);
    });

    test('toJson round-trips', () {
      final config = StabConfig(
        enabled: true,
        mode: DriveMode.sport,
        steeringTrim: 0.03,
      );
      final json = config.toJson();
      expect(json['type'], 'set_stab_config');
      expect(json['enabled'], true);
      expect(json['mode'], 1);
      expect(json['steering_trim'], closeTo(0.03, 0.001));

      final restored = StabConfig.fromJson(json);
      expect(restored.mode, DriveMode.sport);
      expect(restored.steeringTrim, closeTo(0.03, 0.001));
    });

    test('DriveMode.fromValue handles all values', () {
      expect(DriveMode.fromValue(0), DriveMode.normal);
      expect(DriveMode.fromValue(1), DriveMode.sport);
      expect(DriveMode.fromValue(2), DriveMode.drift);
      expect(DriveMode.fromValue(3), DriveMode.kids);
      expect(DriveMode.fromValue(4), DriveMode.directLaw);
      expect(DriveMode.fromValue(99), DriveMode.normal); // fallback
    });
  });
}
