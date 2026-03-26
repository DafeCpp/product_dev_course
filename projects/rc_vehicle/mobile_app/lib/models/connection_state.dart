enum VehicleConnectionStatus {
  disconnected,
  connecting,
  connected,
}

class VehicleConnectionState {
  final VehicleConnectionStatus status;
  final String ipAddress;
  final String? error;

  const VehicleConnectionState({
    this.status = VehicleConnectionStatus.disconnected,
    this.ipAddress = '192.168.4.1',
    this.error,
  });

  VehicleConnectionState copyWith({
    VehicleConnectionStatus? status,
    String? ipAddress,
    String? error,
  }) {
    return VehicleConnectionState(
      status: status ?? this.status,
      ipAddress: ipAddress ?? this.ipAddress,
      error: error,
    );
  }

  bool get isConnected => status == VehicleConnectionStatus.connected;
}
