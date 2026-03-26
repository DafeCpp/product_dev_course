import 'package:flutter_riverpod/flutter_riverpod.dart';

class DriveInput {
  final double throttle;
  final double steering;

  const DriveInput({this.throttle = 0, this.steering = 0});

  DriveInput copyWith({double? throttle, double? steering}) {
    return DriveInput(
      throttle: throttle ?? this.throttle,
      steering: steering ?? this.steering,
    );
  }
}

final driveInputProvider =
    StateNotifierProvider<DriveInputNotifier, DriveInput>(
  (ref) => DriveInputNotifier(),
);

class DriveInputNotifier extends StateNotifier<DriveInput> {
  DriveInputNotifier() : super(const DriveInput());

  void setThrottle(double value) {
    state = state.copyWith(throttle: value.clamp(-1.0, 1.0));
  }

  void setSteering(double value) {
    state = state.copyWith(steering: value.clamp(-1.0, 1.0));
  }

  void reset() {
    state = const DriveInput();
  }
}
