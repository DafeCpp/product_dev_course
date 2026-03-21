import 'dart:async';
import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';
import '../models/telemetry_frame.dart';

class UdpReceiverStats {
  int received = 0;
  int lost = 0;
  int? lastSeq;
  DateTime? lastPacketTime;
  double hz = 0;
  int _hzCounter = 0;
  DateTime _hzWindowStart = DateTime.now();

  void onPacket(int seq) {
    received++;
    _hzCounter++;

    // Detect lost packets via sequence number gap.
    if (lastSeq != null) {
      final gap = seq - lastSeq! - 1;
      if (gap > 0 && gap < 10000) {
        lost += gap;
      }
    }
    lastSeq = seq;
    lastPacketTime = DateTime.now();

    // Compute Hz over 1-second windows.
    final now = DateTime.now();
    final elapsed = now.difference(_hzWindowStart).inMilliseconds;
    if (elapsed >= 1000) {
      hz = _hzCounter * 1000.0 / elapsed;
      _hzCounter = 0;
      _hzWindowStart = now;
    }
  }

  double get lossPercent {
    final total = received + lost;
    if (total == 0) return 0;
    return lost * 100.0 / total;
  }

  void reset() {
    received = 0;
    lost = 0;
    lastSeq = null;
    lastPacketTime = null;
    hz = 0;
    _hzCounter = 0;
    _hzWindowStart = DateTime.now();
  }
}

/// Runs UDP receiver in a separate isolate.
/// Sends parsed TelemetryFrame objects back via SendPort.
class UdpReceiver {
  Isolate? _isolate;
  ReceivePort? _receivePort;
  SendPort? _commandPort;
  final StreamController<TelemetryFrame> _controller =
      StreamController.broadcast();
  final UdpReceiverStats stats = UdpReceiverStats();

  Stream<TelemetryFrame> get stream => _controller.stream;

  Future<void> start(int port) async {
    await stop();
    stats.reset();

    _receivePort = ReceivePort();
    _isolate = await Isolate.spawn(
      _isolateEntry,
      _IsolateConfig(sendPort: _receivePort!.sendPort, port: port),
    );

    _receivePort!.listen((message) {
      if (message is SendPort) {
        _commandPort = message;
      } else if (message is Uint8List) {
        final frame = TelemetryFrame.fromBytes(message);
        if (frame != null) {
          stats.onPacket(frame.seqNum);
          _controller.add(frame);
        }
      }
    });
  }

  Future<void> stop() async {
    _commandPort?.send('stop');
    _isolate?.kill(priority: Isolate.immediate);
    _isolate = null;
    _receivePort?.close();
    _receivePort = null;
    _commandPort = null;
  }

  void dispose() {
    stop();
    _controller.close();
  }
}

class _IsolateConfig {
  final SendPort sendPort;
  final int port;
  const _IsolateConfig({required this.sendPort, required this.port});
}

void _isolateEntry(_IsolateConfig config) async {
  final commandPort = ReceivePort();
  config.sendPort.send(commandPort.sendPort);

  RawDatagramSocket? socket;
  try {
    socket = await RawDatagramSocket.bind(InternetAddress.anyIPv4, config.port);
  } catch (_) {
    return;
  }

  var running = true;
  commandPort.listen((msg) {
    if (msg == 'stop') {
      running = false;
      socket?.close();
    }
  });

  socket.listen((event) {
    if (!running) return;
    if (event == RawSocketEvent.read) {
      final datagram = socket?.receive();
      if (datagram != null && datagram.data.length >= TelemetryFrame.packetSize) {
        config.sendPort.send(Uint8List.fromList(datagram.data));
      }
    }
  });
}
