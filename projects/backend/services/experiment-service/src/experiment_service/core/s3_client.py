"""Async S3 client wrapper (aioboto3)."""
from __future__ import annotations

import aioboto3
from botocore.config import Config
from experiment_service.settings import settings


class S3Client:
    """Thin async wrapper around aioboto3 for presigned URL generation."""

    def __init__(self) -> None:
        self._session = aioboto3.Session()
        self._bucket = settings.s3_bucket
        self._expire = settings.s3_presign_expire_seconds
        self._endpoint = settings.s3_endpoint_url
        self._public_endpoint = settings.s3_public_endpoint_url
        self._access_key = settings.s3_access_key
        self._secret_key = settings.s3_secret_key

    def _client_ctx(self):  # type: ignore[return]
        return self._session.client(
            "s3",
            endpoint_url=self._endpoint,
            aws_access_key_id=self._access_key,
            aws_secret_access_key=self._secret_key,
            config=Config(signature_version="s3v4"),
        )

    async def ensure_bucket(self) -> None:
        """Create bucket if it doesn't exist (idempotent)."""
        async with self._client_ctx() as s3:
            try:
                await s3.head_bucket(Bucket=self._bucket)
            except Exception:
                await s3.create_bucket(Bucket=self._bucket)

    async def presign_upload(self, key: str, content_type: str, expires: int | None = None) -> str:
        """Return presigned PUT URL for uploading an object."""
        exp = expires or self._expire
        async with self._client_ctx() as s3:
            url: str = await s3.generate_presigned_url(
                "put_object",
                Params={"Bucket": self._bucket, "Key": key, "ContentType": content_type},
                ExpiresIn=exp,
            )
        # Replace internal endpoint with public endpoint for browser access
        if self._endpoint != self._public_endpoint:
            url = url.replace(self._endpoint, self._public_endpoint, 1)
        return url

    async def presign_download(self, key: str, filename: str | None = None, expires: int | None = None) -> str:
        """Return presigned GET URL for downloading an object."""
        exp = expires or self._expire
        params: dict = {"Bucket": self._bucket, "Key": key}
        if filename:
            params["ResponseContentDisposition"] = f'attachment; filename="{filename}"'
        async with self._client_ctx() as s3:
            url: str = await s3.generate_presigned_url(
                "get_object",
                Params=params,
                ExpiresIn=exp,
            )
        if self._endpoint != self._public_endpoint:
            url = url.replace(self._endpoint, self._public_endpoint, 1)
        return url

    async def delete_object(self, key: str) -> None:
        """Delete an object from S3."""
        async with self._client_ctx() as s3:
            await s3.delete_object(Bucket=self._bucket, Key=key)


_s3_client: S3Client | None = None


def get_s3_client() -> S3Client:
    global _s3_client
    if _s3_client is None:
        _s3_client = S3Client()
    return _s3_client
