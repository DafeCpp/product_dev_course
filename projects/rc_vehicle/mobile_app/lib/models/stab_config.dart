// Stabilization configuration — mirrors firmware StabilizationConfig.
// JSON keys match firmware's stabilization_config_json.cpp exactly.

enum DriveMode {
  normal(0, 'Normal', 'Yaw rate control + pitch compensation'),
  sport(1, 'Sport', 'Aggressive response, fast corrections'),
  drift(2, 'Drift', 'Soft yaw control, active slip angle PID'),
  kids(3, 'Kids', 'Limited power + anti-spin protection'),
  directLaw(4, 'Direct Law', 'No stabilization, raw input');

  final int value;
  final String label;
  final String description;
  const DriveMode(this.value, this.label, this.description);

  static DriveMode fromValue(int v) =>
      DriveMode.values.firstWhere((m) => m.value == v, orElse: () => normal);
}

class PidConfig {
  double kp, ki, kd, maxIntegral, maxCorrection;

  PidConfig({
    this.kp = 0.1,
    this.ki = 0.0,
    this.kd = 0.005,
    this.maxIntegral = 0.5,
    this.maxCorrection = 0.3,
  });

  factory PidConfig.fromJson(Map<String, dynamic> j) => PidConfig(
        kp: (j['kp'] as num?)?.toDouble() ?? 0.1,
        ki: (j['ki'] as num?)?.toDouble() ?? 0.0,
        kd: (j['kd'] as num?)?.toDouble() ?? 0.005,
        maxIntegral: (j['max_integral'] as num?)?.toDouble() ?? 0.5,
        maxCorrection: (j['max_correction'] as num?)?.toDouble() ?? 0.3,
      );

  Map<String, dynamic> toJson() => {
        'kp': kp,
        'ki': ki,
        'kd': kd,
        'max_integral': maxIntegral,
        'max_correction': maxCorrection,
      };
}

class KidsModeConfig {
  double throttleLimit,
      reverseLimit,
      steeringLimit,
      slewThrottle,
      slewSteering;
  bool antiSpinEnabled;
  double antiSpinThresholdDeg, antiSpinReduction;
  bool accelLimitEnabled;
  double accelThresholdG, accelLimitGain, accelMaxReduction;

  KidsModeConfig({
    this.throttleLimit = 0.3,
    this.reverseLimit = 0.2,
    this.steeringLimit = 0.7,
    this.slewThrottle = 0.3,
    this.slewSteering = 0.5,
    this.antiSpinEnabled = true,
    this.antiSpinThresholdDeg = 10.0,
    this.antiSpinReduction = 0.7,
    this.accelLimitEnabled = true,
    this.accelThresholdG = 0.15,
    this.accelLimitGain = 3.0,
    this.accelMaxReduction = 0.5,
  });

  factory KidsModeConfig.fromJson(Map<String, dynamic> j) => KidsModeConfig(
        throttleLimit: (j['throttle_limit'] as num?)?.toDouble() ?? 0.3,
        reverseLimit: (j['reverse_limit'] as num?)?.toDouble() ?? 0.2,
        steeringLimit: (j['steering_limit'] as num?)?.toDouble() ?? 0.7,
        slewThrottle: (j['slew_throttle'] as num?)?.toDouble() ?? 0.3,
        slewSteering: (j['slew_steering'] as num?)?.toDouble() ?? 0.5,
        antiSpinEnabled: j['anti_spin_enabled'] as bool? ?? true,
        antiSpinThresholdDeg:
            (j['anti_spin_threshold_deg'] as num?)?.toDouble() ?? 10.0,
        antiSpinReduction:
            (j['anti_spin_reduction'] as num?)?.toDouble() ?? 0.7,
        accelLimitEnabled: j['accel_limit_enabled'] as bool? ?? true,
        accelThresholdG:
            (j['accel_threshold_g'] as num?)?.toDouble() ?? 0.15,
        accelLimitGain: (j['accel_limit_gain'] as num?)?.toDouble() ?? 3.0,
        accelMaxReduction:
            (j['accel_max_reduction'] as num?)?.toDouble() ?? 0.5,
      );

  Map<String, dynamic> toJson() => {
        'throttle_limit': throttleLimit,
        'reverse_limit': reverseLimit,
        'steering_limit': steeringLimit,
        'slew_throttle': slewThrottle,
        'slew_steering': slewSteering,
        'anti_spin_enabled': antiSpinEnabled,
        'anti_spin_threshold_deg': antiSpinThresholdDeg,
        'anti_spin_reduction': antiSpinReduction,
        'accel_limit_enabled': accelLimitEnabled,
        'accel_threshold_g': accelThresholdG,
        'accel_limit_gain': accelLimitGain,
        'accel_max_reduction': accelMaxReduction,
      };
}

class StabConfig {
  bool enabled;
  DriveMode mode;
  int fadeMs;

  // Yaw rate PID
  PidConfig yawRatePid;
  double steerToYawRateDps;

  // Slip angle PID
  PidConfig slipAnglePid;
  double slipTargetDeg;

  // Adaptive
  bool adaptiveEnabled;
  double adaptiveSpeedRefMs, adaptiveScaleMin, adaptiveScaleMax;

  // Oversteer
  bool oversteerWarnEnabled;
  double oversteerSlipThreshDeg,
      oversteerRateThreshDegS,
      oversteerThrottleReduction;

  // Pitch compensation
  bool pitchCompEnabled;
  double pitchCompGain, pitchCompMaxCorrection;

  // Kids mode
  KidsModeConfig kidsMode;

  // Trim
  double steeringTrim, throttleTrim;

  // Filter
  double madgwickBeta, lpfCutoffHz;
  bool adaptiveBetaEnabled;

  StabConfig({
    this.enabled = false,
    this.mode = DriveMode.normal,
    this.fadeMs = 500,
    PidConfig? yawRatePid,
    this.steerToYawRateDps = 90.0,
    PidConfig? slipAnglePid,
    this.slipTargetDeg = 0.0,
    this.adaptiveEnabled = false,
    this.adaptiveSpeedRefMs = 1.5,
    this.adaptiveScaleMin = 0.5,
    this.adaptiveScaleMax = 2.0,
    this.oversteerWarnEnabled = false,
    this.oversteerSlipThreshDeg = 20.0,
    this.oversteerRateThreshDegS = 50.0,
    this.oversteerThrottleReduction = 0.0,
    this.pitchCompEnabled = false,
    this.pitchCompGain = 0.01,
    this.pitchCompMaxCorrection = 0.25,
    KidsModeConfig? kidsMode,
    this.steeringTrim = 0.0,
    this.throttleTrim = 0.0,
    this.madgwickBeta = 0.1,
    this.lpfCutoffHz = 30.0,
    this.adaptiveBetaEnabled = true,
  })  : yawRatePid = yawRatePid ?? PidConfig(),
        slipAnglePid = slipAnglePid ?? PidConfig(),
        kidsMode = kidsMode ?? KidsModeConfig();

  factory StabConfig.fromJson(Map<String, dynamic> j) {
    final yr = j['yaw_rate'] as Map<String, dynamic>? ?? {};
    final sa = j['slip_angle'] as Map<String, dynamic>? ?? {};
    final ad = j['adaptive'] as Map<String, dynamic>? ?? {};
    final os = j['oversteer'] as Map<String, dynamic>? ?? {};
    final pc = j['pitch_comp'] as Map<String, dynamic>? ?? {};
    final km = j['kids_mode'] as Map<String, dynamic>? ?? {};
    final fi = j['filter'] as Map<String, dynamic>? ?? {};

    return StabConfig(
      enabled: j['enabled'] as bool? ?? false,
      mode: DriveMode.fromValue((j['mode'] as num?)?.toInt() ?? 0),
      fadeMs: (j['fade_ms'] as num?)?.toInt() ?? 500,
      yawRatePid:
          PidConfig.fromJson(yr['pid'] as Map<String, dynamic>? ?? {}),
      steerToYawRateDps:
          (yr['steer_to_yaw_rate_dps'] as num?)?.toDouble() ?? 90.0,
      slipAnglePid:
          PidConfig.fromJson(sa['pid'] as Map<String, dynamic>? ?? {}),
      slipTargetDeg: (sa['target_deg'] as num?)?.toDouble() ?? 0.0,
      adaptiveEnabled: ad['enabled'] as bool? ?? false,
      adaptiveSpeedRefMs: (ad['speed_ref_ms'] as num?)?.toDouble() ?? 1.5,
      adaptiveScaleMin: (ad['scale_min'] as num?)?.toDouble() ?? 0.5,
      adaptiveScaleMax: (ad['scale_max'] as num?)?.toDouble() ?? 2.0,
      oversteerWarnEnabled: os['warn_enabled'] as bool? ?? false,
      oversteerSlipThreshDeg:
          (os['slip_thresh_deg'] as num?)?.toDouble() ?? 20.0,
      oversteerRateThreshDegS:
          (os['rate_thresh_deg_s'] as num?)?.toDouble() ?? 50.0,
      oversteerThrottleReduction:
          (os['throttle_reduction'] as num?)?.toDouble() ?? 0.0,
      pitchCompEnabled: pc['enabled'] as bool? ?? false,
      pitchCompGain: (pc['gain'] as num?)?.toDouble() ?? 0.01,
      pitchCompMaxCorrection:
          (pc['max_correction'] as num?)?.toDouble() ?? 0.25,
      kidsMode: KidsModeConfig.fromJson(km),
      steeringTrim: (j['steering_trim'] as num?)?.toDouble() ?? 0.0,
      throttleTrim: (j['throttle_trim'] as num?)?.toDouble() ?? 0.0,
      madgwickBeta: (fi['madgwick_beta'] as num?)?.toDouble() ?? 0.1,
      lpfCutoffHz: (fi['lpf_cutoff_hz'] as num?)?.toDouble() ?? 30.0,
      adaptiveBetaEnabled: fi['adaptive_beta_enabled'] as bool? ?? true,
    );
  }

  Map<String, dynamic> toJson() => {
        'type': 'set_stab_config',
        'enabled': enabled,
        'mode': mode.value,
        'fade_ms': fadeMs,
        'yaw_rate': {
          'pid': yawRatePid.toJson(),
          'steer_to_yaw_rate_dps': steerToYawRateDps,
        },
        'slip_angle': {
          'pid': slipAnglePid.toJson(),
          'target_deg': slipTargetDeg,
        },
        'adaptive': {
          'enabled': adaptiveEnabled,
          'speed_ref_ms': adaptiveSpeedRefMs,
          'scale_min': adaptiveScaleMin,
          'scale_max': adaptiveScaleMax,
        },
        'oversteer': {
          'warn_enabled': oversteerWarnEnabled,
          'slip_thresh_deg': oversteerSlipThreshDeg,
          'rate_thresh_deg_s': oversteerRateThreshDegS,
          'throttle_reduction': oversteerThrottleReduction,
        },
        'pitch_comp': {
          'enabled': pitchCompEnabled,
          'gain': pitchCompGain,
          'max_correction': pitchCompMaxCorrection,
        },
        'kids_mode': kidsMode.toJson(),
        'steering_trim': steeringTrim,
        'throttle_trim': throttleTrim,
        'filter': {
          'madgwick_beta': madgwickBeta,
          'lpf_cutoff_hz': lpfCutoffHz,
          'adaptive_beta_enabled': adaptiveBetaEnabled,
        },
      };
}
