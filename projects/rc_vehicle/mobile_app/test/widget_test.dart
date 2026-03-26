import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:rc_vehicle_app/app.dart';

void main() {
  testWidgets('App starts and shows connection screen', (tester) async {
    await tester.pumpWidget(const ProviderScope(child: RcVehicleApp()));
    expect(find.text('RC Vehicle'), findsOneWidget);
    expect(find.text('AP Mode (default)'), findsOneWidget);
  });
}
