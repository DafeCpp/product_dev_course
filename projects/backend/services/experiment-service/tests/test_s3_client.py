"""Tests for the async S3Client wrapper.

We don't run a real S3 here — instead we monkeypatch ``S3Client._client_ctx``
to yield a fake aioboto3-shaped client that records every call. This covers
the wrapper's responsibilities: parameter assembly, presign default-expire
fallback, public-endpoint URL rewriting, and the singleton in get_s3_client.
"""
from __future__ import annotations

from contextlib import asynccontextmanager

import pytest

from experiment_service.core import s3_client as s3_module
from experiment_service.core.s3_client import S3Client, get_s3_client


class _FakeS3:
    """Fake aioboto3 client. Records calls and returns canned URLs."""

    def __init__(self) -> None:
        self.head_bucket_calls: list[dict] = []
        self.create_bucket_calls: list[dict] = []
        self.delete_object_calls: list[dict] = []
        self.presign_calls: list[tuple[str, dict, int]] = []
        self.head_bucket_raises = False
        self.next_url = "http://internal-minio:9000/bucket/key?signature=abc"

    async def head_bucket(self, **kwargs):
        self.head_bucket_calls.append(kwargs)
        if self.head_bucket_raises:
            raise RuntimeError("not found")
        return {}

    async def create_bucket(self, **kwargs):
        self.create_bucket_calls.append(kwargs)
        return {}

    async def delete_object(self, **kwargs):
        self.delete_object_calls.append(kwargs)
        return {}

    async def generate_presigned_url(self, op, Params, ExpiresIn):
        self.presign_calls.append((op, Params, ExpiresIn))
        return self.next_url


def _install_fake(monkeypatch, fake: _FakeS3) -> None:
    @asynccontextmanager
    async def _ctx(self):
        yield fake

    monkeypatch.setattr(S3Client, "_client_ctx", _ctx)


@pytest.fixture
def fake_s3() -> _FakeS3:
    return _FakeS3()


@pytest.fixture
def s3(monkeypatch, fake_s3) -> S3Client:
    """Return an S3Client wired to the fake. Endpoints differ so URL rewriting fires."""
    from experiment_service.settings import settings

    monkeypatch.setattr(settings, "s3_bucket", "test-bucket")
    monkeypatch.setattr(settings, "s3_endpoint_url", "http://internal-minio:9000")
    monkeypatch.setattr(settings, "s3_public_endpoint_url", "http://public.example.com")
    monkeypatch.setattr(settings, "s3_presign_expire_seconds", 600)
    monkeypatch.setattr(settings, "s3_access_key", "ak")
    monkeypatch.setattr(settings, "s3_secret_key", "sk")

    client = S3Client()
    _install_fake(monkeypatch, fake_s3)
    return client


# ---------------------------------------------------------------------------
# ensure_bucket
# ---------------------------------------------------------------------------


async def test_ensure_bucket_skips_create_when_head_succeeds(s3, fake_s3):
    fake_s3.head_bucket_raises = False
    await s3.ensure_bucket()
    assert fake_s3.head_bucket_calls == [{"Bucket": "test-bucket"}]
    assert fake_s3.create_bucket_calls == []


async def test_ensure_bucket_creates_when_head_raises(s3, fake_s3):
    fake_s3.head_bucket_raises = True
    await s3.ensure_bucket()
    assert fake_s3.head_bucket_calls == [{"Bucket": "test-bucket"}]
    assert fake_s3.create_bucket_calls == [{"Bucket": "test-bucket"}]


# ---------------------------------------------------------------------------
# presign_upload
# ---------------------------------------------------------------------------


async def test_presign_upload_passes_params_and_default_expire(s3, fake_s3):
    fake_s3.next_url = "http://internal-minio:9000/test-bucket/key?sig=1"
    url = await s3.presign_upload("dir/file.bin", content_type="application/octet-stream")
    op, params, expires = fake_s3.presign_calls[0]
    assert op == "put_object"
    assert params == {
        "Bucket": "test-bucket",
        "Key": "dir/file.bin",
        "ContentType": "application/octet-stream",
    }
    assert expires == 600  # default from settings.s3_presign_expire_seconds
    # endpoint rewriting kicked in
    assert url == "http://public.example.com/test-bucket/key?sig=1"


async def test_presign_upload_honors_custom_expire(s3, fake_s3):
    await s3.presign_upload("k", content_type="text/plain", expires=42)
    _, _, expires = fake_s3.presign_calls[0]
    assert expires == 42


async def test_presign_upload_skips_rewrite_when_endpoints_equal(monkeypatch, fake_s3):
    from experiment_service.settings import settings

    monkeypatch.setattr(settings, "s3_bucket", "b")
    monkeypatch.setattr(settings, "s3_endpoint_url", "http://same:9000")
    monkeypatch.setattr(settings, "s3_public_endpoint_url", "http://same:9000")
    monkeypatch.setattr(settings, "s3_presign_expire_seconds", 60)
    monkeypatch.setattr(settings, "s3_access_key", "ak")
    monkeypatch.setattr(settings, "s3_secret_key", "sk")

    client = S3Client()
    _install_fake(monkeypatch, fake_s3)

    fake_s3.next_url = "http://same:9000/b/k?x=1"
    url = await client.presign_upload("k", content_type="text/plain")
    assert url == "http://same:9000/b/k?x=1"  # untouched


# ---------------------------------------------------------------------------
# presign_download
# ---------------------------------------------------------------------------


async def test_presign_download_default_no_filename(s3, fake_s3):
    await s3.presign_download("k")
    op, params, expires = fake_s3.presign_calls[0]
    assert op == "get_object"
    assert params == {"Bucket": "test-bucket", "Key": "k"}
    assert "ResponseContentDisposition" not in params
    assert expires == 600


async def test_presign_download_with_filename_sets_content_disposition(s3, fake_s3):
    await s3.presign_download("k", filename="report.csv")
    _, params, _ = fake_s3.presign_calls[0]
    assert params["ResponseContentDisposition"] == 'attachment; filename="report.csv"'


async def test_presign_download_honors_custom_expire(s3, fake_s3):
    await s3.presign_download("k", expires=15)
    _, _, expires = fake_s3.presign_calls[0]
    assert expires == 15


async def test_presign_download_rewrites_endpoint(s3, fake_s3):
    fake_s3.next_url = "http://internal-minio:9000/test-bucket/k?sig=z"
    url = await s3.presign_download("k")
    assert url == "http://public.example.com/test-bucket/k?sig=z"


# ---------------------------------------------------------------------------
# delete_object
# ---------------------------------------------------------------------------


async def test_delete_object_calls_with_bucket_and_key(s3, fake_s3):
    await s3.delete_object("dir/obj.bin")
    assert fake_s3.delete_object_calls == [
        {"Bucket": "test-bucket", "Key": "dir/obj.bin"}
    ]


# ---------------------------------------------------------------------------
# get_s3_client singleton
# ---------------------------------------------------------------------------


def test_get_s3_client_returns_singleton(monkeypatch):
    # Reset the module-level cache.
    monkeypatch.setattr(s3_module, "_s3_client", None, raising=False)
    a = get_s3_client()
    b = get_s3_client()
    assert a is b
    assert isinstance(a, S3Client)
