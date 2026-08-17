from pathlib import Path

import pytest

from simlib import (
    find_invariant_violations,
    find_sim_host,
    load_telemetry_csv,
    run_batch,
)

GOLDEN = Path(__file__).parent / "fixtures" / "rides" / "golden_synth_drive.csv"
FAILSAFE = (Path(__file__).parent / "fixtures" / "rides" /
            "golden_real_failsafe_reconstructed.csv")
STATIC_TILT = (Path(__file__).parent / "fixtures" / "rides" /
               "golden_real_static_tilt.csv")


def test_loader_reads_golden():
    frames = load_telemetry_csv(str(GOLDEN))
    assert len(frames) > 100
    f = frames[10]
    assert f.rc_present and f.mag_present
    assert f.dt_ms > 0


def test_loader_dt_from_timestamps():
    frames = load_telemetry_csv(str(GOLDEN))
    # 100 Гц лог → dt ≈ 10 мс (первый кадр = 2 мс по умолчанию)
    assert all(fr.dt_ms == 10 for fr in frames[1:])


def test_loader_prefers_explicit_fixture_dt():
    frames = load_telemetry_csv(str(STATIC_TILT))
    assert frames[0].dt_ms == 12


def test_loader_reads_explicit_source_presence():
    frames = load_telemetry_csv(str(FAILSAFE))
    assert frames
    assert all(fr.mag_present for fr in frames)
    assert all(not fr.rc_present and not fr.wifi_present for fr in frames)


@pytest.mark.skipif(find_sim_host() is None,
                    reason="sim_host не собран (cmake build tests/), см. FW-S2.1")
def test_replay_invariants_hold():
    frames = load_telemetry_csv(str(GOLDEN))
    out = run_batch(frames, find_sim_host(), identity_calib=True)
    assert len(out) == len(frames)
    violations = find_invariant_violations(out)
    assert not violations, violations[:5]
