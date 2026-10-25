"""Full experiment export against a real database, including telemetry and serialisation helpers."""
from __future__ import annotations

import io
import uuid
import zipfile
from datetime import UTC, datetime

from experiment_service.services import full_export
from tests.test_api_telemetry_export import _insert_telemetry, _setup_context, _ts


async def test_export_includes_telemetry_of_every_session(service_client, pgsql):
    ctx = await _setup_context(service_client)
    await _insert_telemetry(
        pgsql,
        project_id=ctx["project_id"],
        sensor_id=ctx["sensor_uuid"],
        run_id=uuid.UUID(ctx["run_id"]),
        capture_session_id=uuid.UUID(ctx["capture_session_id"]),
        records=[
            {"timestamp": _ts(0), "raw_value": 1.0, "physical_value": 10.0, "conversion_status": "converted"},
            {"timestamp": _ts(1), "raw_value": 2.0},
        ],
    )

    response = await service_client.get(
        f"/api/v1/experiments/{ctx['experiment_id']}/export?project_id={ctx['project_id']}",
        headers=ctx["headers"],
    )
    assert response.status == 200
    archive = zipfile.ZipFile(io.BytesIO(await response.read()))
    telemetry_files = [name for name in archive.namelist() if "/telemetry/sensor_" in name]
    assert len(telemetry_files) == 1
    assert archive.read(telemetry_files[0])


def test_rows_fall_back_to_csv_without_parquet(monkeypatch):
    monkeypatch.setattr(full_export, "HAS_PARQUET", False)
    data, ext = full_export._to_parquet_or_csv([{"a": 1, "b": 2}, {"a": 3}], ["a", "b"])
    assert ext == ".csv"
    assert data.decode().splitlines() == ["a,b", "1,2", "3,"]


def test_empty_rows_always_produce_csv():
    data, ext = full_export._to_parquet_or_csv([], ["a", "b"])
    assert (data.decode().strip(), ext) == ("a,b", ".csv")


def test_serialize_handles_each_value_kind():
    moment = datetime(2025, 6, 1, tzinfo=UTC)
    identifier = uuid.uuid4()
    assert full_export._serialize(moment) == moment.isoformat()
    assert full_export._serialize(identifier) == str(identifier)
    assert full_export._serialize(5) == 5
    record = {"when": moment, "id": identifier, "n": 1}
    assert full_export._record_to_dict(record, ["when", "id", "n"]) == {
        "when": moment.isoformat(),
        "id": str(identifier),
        "n": 1,
    }
