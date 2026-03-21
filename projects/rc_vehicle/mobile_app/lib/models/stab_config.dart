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
  final double kp, ki, kd, maxIntegral, maxCorrection;

  const PidConfig({
    this.kp = 0.1,
    this.ki = 0.0,
    this.kd = 0.005,
    this.maxIntegral = 0.5,
    this.maxCorrection = 0.3,
  });

  PidConfig copyWith({
    double? kp,
    double? ki,
    double? kd,
    double? maxIntegral,
    double? maxCorrection,
  }) {
    return PidConfig(
      kp: kp ?? this.kp,
      ki: ki ?? this.ki,
      kd: kd ?? this.kd,
      maxIntegral: maxIntegral ?? this.maxIntegral,
      maxCorrection: maxCorrection ?? this.maxCorrection,
    );
  }

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
  final double throttleLimit,
      reverseLimit,
      steeringLimit,
      slewThrottle,
      slewSteering;
  final bool antiSpinEnabled;
  final double antiSpinThresholdDeg, antiSpinReduction;
  final bool accelLimitEnabled;
  final double accelThresholdG, accelLimitGain, accelMaxReduction;

  const KidsModeConfig({
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

  KidsModeConfig copyWith({
    double? throttleLimit,
    double? reverseLimit,
    double? steeringLimit,
    double? slewThrottle,
    double? slewSteering,
    bool? antiSpinEnabled,
    double? antiSpinThresholdDeg,
    double? antiSpinReduction,
    bool? accelLimitEnabled,
    double? accelThresholdG,
    double? accelLimitGain,
    double? accelMaxReduction,
  }) {
    return KidsModeConfig(
      throttleLimit: throttleLimit ?? this.throttleLimit,
      reverseLimit: reverseLimit ?? this.reverseLimit,
      steeringLimit: steeringLimit ?? this.steeringLimit,
      slewThrottle: slewThrottle ?? this.slewThrottle,
      slewSteering: slewSteering ?? this.slewSteering,
      antiSpinEnabled: antiSpinEnabled ?? this.antiSpinEnabled,
      antiSpinThresholdDeg: antiSpinThresholdDeg ?? this.antiSpinThresholdDeg,
      antiSpinReduction: antiSpinReduction ?? this.antiSpinReduction,
      accelLimitEnabled: accelLimitEnabled ?? this.accelLimitEnabled,
      accelThresholdG: accelThresholdG ?? this.accelThresholdG,
      accelLimitGain: accelLimitGain ?? this.accelLimitGain,
      accelMaxReduction: accelMaxReduction ?? this.accelMaxReduction,
    );
  }

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
  final bool enabled;
  final DriveMode mode;
  final int fadeMs;

  // Yaw rate PID
  final PidConfig yawRatePid;
  final double steerToYawRateDps;

  // Slip angle PID
  final PidConfig slipAnglePid;
  final double slipTargetDeg;

  // Adaptive
  final bool adaptiveEnabled;
  final double adaptiveSpeedRefMs, adaptiveScaleMin, adaptiveScaleMax;

  // Oversteer
  final bool oversteerWarnEnabled;
  final double oversteerSlipThreshDeg,
      oversteerRateThreshDegS,
      oversteerThrottleReduction;

  // Pitch compensation
  final bool pitchCompEnabled;
  final double pitchCompGain, pitchCompMaxCorrection;

  // Kids mode
  final KidsModeConfig kidsMode;

  // Trim
  final double steeringTrim, throttleTrim;

  // Filter
  final double madgwickBeta, lpfCutoffHz;
  final bool adaptiveBetaEnabled;

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
  })  : yawRatePid = yawRatePid ?? const PidConfig(),
        slipAnglePid = slipAnglePid ?? const PidConfig(),
        kidsMode = kidsMode ?? const KidsModeConfig();

  StabConfig copyWith({
    bool? enabled,
    DriveMode? mode,
    int? fadeMs,
    PidConfig? yawRatePid,
    double? steerToYawRateDps,
    PidConfig? slipAnglePid,
    double? slipTargetDeg,
    bool? adaptiveEnabled,
    double? adaptiveSpeedRefMs,
    double? adaptiveScaleMin,
    double? adaptiveScaleMax,
    bool? oversteerWarnEnabled,
    double? oversteerSlipThreshDeg,
    double? oversteerRateThreshDegS,
    double? oversteerThrottleReduction,
    bool? pitchCompEnabled,
    double? pitchCompGain,
    double? pitchCompMaxCorrection,
    KidsModeConfig? kidsMode,
    double? steeringTrim,
    double? throttleTrim,
    double? madgwickBeta,
    double? lpfCutoffHz,
    bool? adaptiveBetaEnabled,
  }) {
    return StabConfig(
      enabled: enabled ?? this.enabled,
      mode: mode ?? this.mode,
      fadeMs: fadeMs ?? this.fadeMs,
      yawRatePid: yawRatePid ?? this.yawRatePid,
      steerToYawRateDps: steerToYawRateDps ?? this.steerToYawRateDps,
      slipAnglePid: slipAnglePid ?? this.slipAnglePid,
      slipTargetDeg: slipTargetDeg ?? this.slipTargetDeg,
      adaptiveEnabled: adaptiveEnabled ?? this.adaptiveEnabled,
      adaptiveSpeedRefMs: adaptiveSpeedRefMs ?? this.adaptiveSpeedRefMs,
      adaptiveScaleMin: adaptiveScaleMin ?? this.adaptiveScaleMin,
      adaptiveScaleMax: adaptiveScaleMax ?? this.adaptiveScaleMax,
      oversteerWarnEnabled: oversteerWarnEnabled ?? this.oversteerWarnEnabled,
      oversteerSlipThreshDeg:
          oversteerSlipThreshDeg ?? this.oversteerSlipThreshDeg,
      oversteerRateThreshDegS:
          oversteerRateThreshDegS ?? this.oversteerRateThreshDegS,
      oversteerThrottleReduction:
          oversteerThrottleReduction ?? this.oversteerThrottleReduction,
      pitchCompEnabled: pitchCompEnabled ?? this.pitchCompEnabled,
      pitchCompGain: pitchCompGain ?? this.pitchCompGain,
      pitchCompMaxCorrection:
          pitchCompMaxCorrection ?? this.pitchCompMaxCorrection,
      kidsMode: kidsMode ?? this.kidsMode,
      steeringTrim: steeringTrim ?? this.steeringTrim,
      throttleTrim: throttleTrim ?? this.throttleTrim,
      madgwickBeta: madgwickBeta ?? this.madgwickBeta,
      lpfCutoffHz: lpfCutoffHz ?? this.lpfCutoffHz,
      adaptiveBetaEnabled: adaptiveBetaEnabled ?? this.adaptiveBetaEnabled,
    );
  }

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
