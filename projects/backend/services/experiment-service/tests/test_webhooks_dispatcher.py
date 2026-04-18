"""Unit tests for experiment_service.webhooks_dispatcher module."""
from __future__ import annotations

import asyncio
import json
from datetime import datetime, timedelta, timezone
from unittest.mock import AsyncMock, MagicMock, patch
from uuid import uuid4

import pytest
from aiohttp import ClientSession, ClientTimeout, web

from experiment_service.domain.webhooks import WebhookDelivery
from experiment_service.settings import settings
from experiment_service.webhooks_dispatcher import (
    _backoff_seconds,
    _deliver,
    _signature,
    start_webhook_dispatcher,
    stop_webhook_dispatcher,
)


class TestSignature:
    """Tests for _signature function."""

    def test_generates_signature(self):
        """Test _signature generates a signature."""
        secret = "test-secret"
        body = b'{"event": "test"}'

        sig = _signature(secret, body)

        assert sig is not None
        assert isinstance(sig, str)
        assert sig.startswith("sha256=")

    def test_signature_format(self):
        """Test signature has correct format."""
        secret = "test-secret"
        body = b'{"event": "test"}'

        sig = _signature(secret, body)

        # Should be sha256=<64-hex-chars>
        parts = sig.split("=")
        assert len(parts) == 2
        assert parts[0] == "sha256"
        assert len(parts[1]) == 64  # SHA256 produces 64 hex chars
        assert all(c in "0123456789abcdef" for c in parts[1])

    def test_same_input_same_signature(self):
        """Test same input produces same signature."""
        secret = "test-secret"
        body = b'{"event": "test"}'

        sig1 = _signature(secret, body)
        sig2 = _signature(secret, body)

        assert sig1 == sig2

    def test_different_secret_different_signature(self):
        """Test different secret produces different signature."""
        body = b'{"event": "test"}'

        sig1 = _signature("secret-1", body)
        sig2 = _signature("secret-2", body)

        assert sig1 != sig2

    def test_different_body_different_signature(self):
        """Test different body produces different signature."""
        secret = "test-secret"

        sig1 = _signature(secret, b'{"event": "test1"}')
        sig2 = _signature(secret, b'{"event": "test2"}')

        assert sig1 != sig2

    def test_empty_body(self):
        """Test signature with empty body."""
        secret = "test-secret"
        body = b""

        sig = _signature(secret, body)

        assert sig.startswith("sha256=")
        assert len(sig) == 71  # sha256= + 64 hex chars

    def test_unicode_body(self):
        """Test signature with unicode in body."""
        secret = "test-secret"
        body = '{"event": "тест", "data": "测试"}'.encode("utf-8")

        sig = _signature(secret, body)

        assert sig.startswith("sha256=")
        assert len(sig) == 71

    def test_known_signature(self):
        """Test signature against known value."""
        secret = "test"
        body = b"hello"

        sig = _signature(secret, body)

        # Pre-computed HMAC-SHA256
        import hmac
        from hashlib import sha256

        expected = "sha256=" + hmac.new(
            secret.encode("utf-8"), body, sha256
        ).hexdigest()

        assert sig == expected


class MockTask:
    """Mock asyncio.Task that can be awaited."""
    
    def __init__(self, cancelled: bool = False, done: bool = True):
        self._cancelled = cancelled
        self._done = done
        self.cancel_called = False
    
    def cancel(self):
        self.cancel_called = True
    
    def done(self):
        return self._done
    
    def __await__(self):
        # Make the task awaitable
        if self._cancelled:
            raise asyncio.CancelledError()
        yield
        return None




class TestBackoffSeconds:
    """Tests for _backoff_seconds function."""

    def test_attempt_1(self):
        """Test backoff for attempt 1."""
        assert _backoff_seconds(1) == 2  # 2^1

    def test_attempt_2(self):
        """Test backoff for attempt 2."""
        assert _backoff_seconds(2) == 4  # 2^2

    def test_attempt_3(self):
        """Test backoff for attempt 3."""
        assert _backoff_seconds(3) == 8  # 2^3

    def test_attempt_4(self):
        """Test backoff for attempt 4."""
        assert _backoff_seconds(4) == 16  # 2^4

    def test_attempt_5(self):
        """Test backoff for attempt 5."""
        assert _backoff_seconds(5) == 32  # 2^5

    def test_attempt_6(self):
        """Test backoff for attempt 6."""
        assert _backoff_seconds(6) == 60  # 2^6=64, but capped at 60

    def test_attempt_7(self):
        """Test backoff for attempt 7 (capped)."""
        assert _backoff_seconds(7) == 60  # Capped at 60

    def test_attempt_10(self):
        """Test backoff for attempt 10 (capped)."""
        assert _backoff_seconds(10) == 60  # Capped at 60

    def test_attempt_0(self):
        """Test backoff for attempt 0."""
        assert _backoff_seconds(0) == 1  # 2^0

    def test_exponential_growth(self):
        """Test exponential growth pattern."""
        for i in range(1, 6):
            assert _backoff_seconds(i) == 2 ** i

    def test_cap_at_60(self):
        """Test backoff is capped at 60 seconds."""
        for i in range(6, 20):
            assert _backoff_seconds(i) == 60


class TestDeliver:
    """Tests for _deliver function."""

    @pytest.mark.asyncio
    async def test_successful_delivery(self):
        """Test successful webhook delivery."""
        session = MagicMock()
        response = AsyncMock()
        response.text = AsyncMock(return_value="")
        response.status = 200
        response.text = AsyncMock(return_value="")
        mock_cm = MagicMock()
        mock_cm.__aenter__ = AsyncMock(return_value=response)
        mock_cm.__aexit__ = AsyncMock(return_value=None)
        session.post.return_value = mock_cm

        delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret=None,
            request_body={"event": "test"},
            status="pending",
            attempt_count=1,
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        ok, err = await _deliver(session, delivery, timeout_s=5.0)

        assert ok is True
        assert err is None
        session.post.assert_called_once()

    @pytest.mark.asyncio
    async def test_successful_delivery_with_secret(self):
        """Test successful delivery includes signature header."""
        session = MagicMock()
        response = AsyncMock()
        response.text = AsyncMock(return_value="")
        response.status = 200
        mock_cm = MagicMock()
        mock_cm.__aenter__ = AsyncMock(return_value=response)
        mock_cm.__aexit__ = AsyncMock(return_value=None)
        session.post.return_value = mock_cm

        delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret="test-secret",
            request_body={"event": "test"},
            status="pending",
            attempt_count=1,
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        ok, err = await _deliver(session, delivery, timeout_s=5.0)

        assert ok is True
        assert err is None

        # Check headers were set
        call_args = session.post.call_args
        headers = call_args.kwargs["headers"]
        assert "X-Webhook-Signature" in headers
        assert headers["X-Webhook-Signature"].startswith("sha256=")

    @pytest.mark.asyncio
    async def test_4xx_status_returns_error(self):
        """Test 4xx status returns error."""
        session = MagicMock()
        response = AsyncMock()
        response.text = AsyncMock(return_value="")
        response.status = 400
        response.text = AsyncMock(return_value="Bad request")
        mock_cm = MagicMock()
        mock_cm.__aenter__ = AsyncMock(return_value=response)
        mock_cm.__aexit__ = AsyncMock(return_value=None)
        session.post.return_value = mock_cm

        delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret=None,
            request_body={"event": "test"},
            status="pending",
            attempt_count=1,
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        ok, err = await _deliver(session, delivery, timeout_s=5.0)

        assert ok is False
        assert err is not None
        assert "HTTP 400" in err

    @pytest.mark.asyncio
    async def test_5xx_status_returns_error(self):
        """Test 5xx status returns error."""
        session = MagicMock()
        response = AsyncMock()
        response.text = AsyncMock(return_value="")
        response.status = 500
        response.text = AsyncMock(return_value="Internal server error")
        mock_cm = MagicMock()
        mock_cm.__aenter__ = AsyncMock(return_value=response)
        mock_cm.__aexit__ = AsyncMock(return_value=None)
        session.post.return_value = mock_cm

        delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret=None,
            request_body={"event": "test"},
            status="pending",
            attempt_count=1,
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        ok, err = await _deliver(session, delivery, timeout_s=5.0)

        assert ok is False
        assert err is not None
        assert "HTTP 500" in err

    @pytest.mark.asyncio
    async def test_network_error_returns_error(self):
        """Test network error returns error."""
        session = MagicMock()
        session.post.side_effect = ConnectionError("Connection refused")

        delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret=None,
            request_body={"event": "test"},
            status="pending",
            attempt_count=1,
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        ok, err = await _deliver(session, delivery, timeout_s=5.0)

        assert ok is False
        assert err is not None
        assert "Connection refused" in err

    @pytest.mark.asyncio
    async def test_timeout_error_returns_error(self):
        """Test timeout error returns error."""
        session = MagicMock()
        session.post.side_effect = TimeoutError("Request timed out")

        delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret=None,
            request_body={"event": "test"},
            status="pending",
            attempt_count=1,
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        ok, err = await _deliver(session, delivery, timeout_s=5.0)

        assert ok is False
        assert err is not None

    @pytest.mark.asyncio
    async def test_sends_correct_headers(self):
        """Test correct headers are sent."""
        session = MagicMock()
        response = AsyncMock()
        response.text = AsyncMock(return_value="")
        response.status = 200
        mock_cm = MagicMock()
        mock_cm.__aenter__ = AsyncMock(return_value=response)
        mock_cm.__aexit__ = AsyncMock(return_value=None)
        session.post.return_value = mock_cm

        delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret=None,
            request_body={"event": "test"},
            status="pending",
            attempt_count=1,
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        await _deliver(session, delivery, timeout_s=5.0)

        call_args = session.post.call_args
        headers = call_args.kwargs["headers"]
        assert headers["Content-Type"] == "application/json"
        assert headers["X-Webhook-Event"] == "test.event"
        assert "X-Webhook-Delivery-Id" in headers

    @pytest.mark.asyncio
    async def test_sends_correct_body(self):
        """Test correct body is sent."""
        session = MagicMock()
        response = AsyncMock()
        response.text = AsyncMock(return_value="")
        response.status = 200
        mock_cm = MagicMock()
        mock_cm.__aenter__ = AsyncMock(return_value=response)
        mock_cm.__aexit__ = AsyncMock(return_value=None)
        session.post.return_value = mock_cm

        delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret=None,
            request_body={"key": "value", "nested": {"a": 1}},
            status="pending",
            attempt_count=1,
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        await _deliver(session, delivery, timeout_s=5.0)

        call_args = session.post.call_args
        data = call_args.kwargs["data"]
        # Should be compact JSON (as bytes)
        assert data == json.dumps(
            {"key": "value", "nested": {"a": 1}},
            separators=(",", ":"),
            ensure_ascii=False,
        ).encode("utf-8")

    @pytest.mark.asyncio
    async def test_truncates_long_error_messages(self):
        """Test long error messages are truncated."""
        session = MagicMock()
        response = AsyncMock()
        response.text = AsyncMock(return_value="")
        response.status = 500
        long_text = "x" * 3000
        response.text = AsyncMock(return_value=long_text)
        mock_cm = MagicMock()
        mock_cm.__aenter__ = AsyncMock(return_value=response)
        mock_cm.__aexit__ = AsyncMock(return_value=None)
        session.post.return_value = mock_cm

        delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret=None,
            request_body={"event": "test"},
            status="pending",
            attempt_count=1,
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        ok, err = await _deliver(session, delivery, timeout_s=5.0)

        assert ok is False
        assert err is not None
        assert len(err) <= 2013  # "HTTP 500: " + 2000 chars

    @pytest.mark.asyncio
    async def test_uses_timeout(self):
        """Test timeout is used."""
        session = MagicMock()
        response = AsyncMock()
        response.text = AsyncMock(return_value="")
        response.status = 200
        mock_cm = MagicMock()
        mock_cm.__aenter__ = AsyncMock(return_value=response)
        mock_cm.__aexit__ = AsyncMock(return_value=None)
        session.post.return_value = mock_cm

        delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret=None,
            request_body={"event": "test"},
            status="pending",
            attempt_count=1,
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        await _deliver(session, delivery, timeout_s=10.0)

        call_args = session.post.call_args
        timeout = call_args.kwargs["timeout"]
        assert isinstance(timeout, ClientTimeout)
        assert timeout.total == 10.0


class TestStartWebhookDispatcher:
    """Tests for start_webhook_dispatcher function."""

    @pytest.mark.asyncio
    async def test_creates_client_session(self):
        """Test start_webhook_dispatcher creates ClientSession."""
        app = web.Application()

        with patch("experiment_service.webhooks_dispatcher.ClientSession") as MockSession:
            mock_session = MagicMock()
            MockSession.return_value = mock_session

            await start_webhook_dispatcher(app)

            MockSession.assert_called_once()
            assert app["webhook_http_session"] is mock_session

    @pytest.mark.asyncio
    async def test_creates_dispatcher_task(self):
        """Test start_webhook_dispatcher creates dispatcher task."""
        app = web.Application()

        with patch("experiment_service.webhooks_dispatcher.ClientSession"):
            await start_webhook_dispatcher(app)

            assert "webhook_dispatcher_task" in app
            task = app["webhook_dispatcher_task"]
            assert isinstance(task, asyncio.Task)
            assert not task.done()

    @pytest.mark.asyncio
    async def test_session_uses_correct_timeout(self):
        """Test ClientSession uses correct timeout."""
        app = web.Application()

        with patch("experiment_service.webhooks_dispatcher.ClientSession") as MockSession:
            mock_session = MagicMock()
            MockSession.return_value = mock_session

            await start_webhook_dispatcher(app)

            call_args = MockSession.call_args
            timeout = call_args.kwargs["timeout"]
            assert isinstance(timeout, ClientTimeout)
            assert timeout.total == settings.webhook_request_timeout_seconds


class TestStopWebhookDispatcher:
    """Tests for stop_webhook_dispatcher function."""

    @pytest.mark.asyncio
    async def test_cancels_dispatcher_task(self):
        """Test stop_webhook_dispatcher cancels task."""
        app = web.Application()
        mock_task = MockTask(cancelled=True, done=True)
        app["webhook_dispatcher_task"] = mock_task

        await stop_webhook_dispatcher(app)

        assert mock_task.cancel_called is True

    @pytest.mark.asyncio
    async def test_closes_client_session(self):
        """Test stop_webhook_dispatcher closes session."""
        app = web.Application()
        mock_session = AsyncMock()
        app["webhook_http_session"] = mock_session

        await stop_webhook_dispatcher(app)

        mock_session.close.assert_awaited_once()

    @pytest.mark.asyncio
    async def test_handles_missing_task(self):
        """Test stop_webhook_dispatcher handles missing task."""
        app = web.Application()
        mock_session = AsyncMock()
        app["webhook_http_session"] = mock_session

        # Should not raise
        await stop_webhook_dispatcher(app)

    @pytest.mark.asyncio
    async def test_handles_missing_session(self):
        """Test stop_webhook_dispatcher handles missing session."""
        app = web.Application()
        mock_task = MockTask(cancelled=True, done=True)
        app["webhook_dispatcher_task"] = mock_task

        # Should not raise
        await stop_webhook_dispatcher(app)

    @pytest.mark.asyncio
    async def test_handles_cancelled_error(self):
        """Test stop_webhook_dispatcher handles CancelledError."""
        app = web.Application()
        mock_task = MockTask(cancelled=True, done=True)
        app["webhook_dispatcher_task"] = mock_task

        mock_session = AsyncMock()
        app["webhook_http_session"] = mock_session

        # Should not raise
        await stop_webhook_dispatcher(app)


class TestWebhookDispatcherLoop:
    """Tests for webhook dispatcher loop."""

    @pytest.mark.asyncio
    async def test_dispatcher_claims_and_delivers(self):
        """Test dispatcher claims due deliveries and delivers them."""
        app = web.Application()
        mock_session = MagicMock()
        app["webhook_http_session"] = mock_session

        # Mock response
        response = AsyncMock()
        response.text = AsyncMock(return_value="")
        response.status = 200
        mock_cm = MagicMock()
        mock_cm.__aenter__ = AsyncMock(return_value=response)
        mock_cm.__aexit__ = AsyncMock(return_value=None)
        mock_session.post.return_value = mock_cm

        # Mock repository
        mock_delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret=None,
            request_body={"event": "test"},
            status="pending",
            attempt_count=1,
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        with patch("experiment_service.webhooks_dispatcher.get_pool") as mock_get_pool, \
             patch("experiment_service.webhooks_dispatcher.WebhookDeliveryRepository") as MockRepo:

            mock_pool = MagicMock()
            mock_get_pool.return_value = mock_pool
            
            mock_repo = MagicMock()
            mock_repo.claim_due_pending = AsyncMock(return_value=[mock_delivery])
            mock_repo.mark_attempt = AsyncMock()
            MockRepo.return_value = mock_repo

            # Run dispatcher loop for a short time
            from experiment_service.webhooks_dispatcher import _dispatcher_loop

            # Create task and cancel after short delay
            task = asyncio.create_task(_dispatcher_loop(app))
            await asyncio.sleep(0.1)
            task.cancel()

            try:
                await task
            except asyncio.CancelledError:
                pass

            # Verify delivery was attempted
            mock_repo.claim_due_pending.assert_called()

    @pytest.mark.asyncio
    async def test_dispatcher_handles_empty_queue(self):
        """Test dispatcher handles empty queue."""
        app = web.Application()
        mock_session = MagicMock()
        app["webhook_http_session"] = mock_session

        with patch("experiment_service.webhooks_dispatcher.get_pool") as mock_get_pool, \
             patch("experiment_service.webhooks_dispatcher.WebhookDeliveryRepository") as MockRepo:

            mock_pool = MagicMock()
            mock_get_pool.return_value = mock_pool
            
            mock_repo = MagicMock()
            mock_repo.claim_due_pending = AsyncMock(return_value=[])
            MockRepo.return_value = mock_repo

            from experiment_service.webhooks_dispatcher import _dispatcher_loop

            # Run for short time
            task = asyncio.create_task(_dispatcher_loop(app))
            await asyncio.sleep(0.05)
            task.cancel()

            try:
                await task
            except asyncio.CancelledError:
                pass

            # Should have claimed (even if empty)
            mock_repo.claim_due_pending.assert_called()

    @pytest.mark.asyncio
    async def test_dispatcher_marks_successful_delivery(self):
        """Test dispatcher marks successful deliveries."""
        app = web.Application()
        mock_session = MagicMock()
        response = AsyncMock()
        response.text = AsyncMock(return_value="")
        response.status = 200
        mock_cm = MagicMock()
        mock_cm.__aenter__ = AsyncMock(return_value=response)
        mock_cm.__aexit__ = AsyncMock(return_value=None)
        mock_session.post.return_value = mock_cm
        app["webhook_http_session"] = mock_session

        mock_delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret=None,
            request_body={"event": "test"},
            status="pending",
            attempt_count=1,
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        with patch("experiment_service.webhooks_dispatcher.get_pool") as mock_get_pool, \
             patch("experiment_service.webhooks_dispatcher.WebhookDeliveryRepository") as MockRepo:

            mock_pool = MagicMock()
            mock_get_pool.return_value = mock_pool
            
            mock_repo = MagicMock()
            mock_repo.claim_due_pending = AsyncMock(return_value=[mock_delivery])
            mock_repo.mark_attempt = AsyncMock()
            MockRepo.return_value = mock_repo

            from experiment_service.webhooks_dispatcher import _dispatcher_loop

            task = asyncio.create_task(_dispatcher_loop(app))
            await asyncio.sleep(0.1)
            task.cancel()

            try:
                await task
            except asyncio.CancelledError:
                pass

            # Verify mark_attempt was called with success
            mock_repo.mark_attempt.assert_called()
            call_args = mock_repo.mark_attempt.call_args
            assert call_args.kwargs["success"] is True
            assert call_args.kwargs["status"] == "succeeded"

    @pytest.mark.asyncio
    async def test_dispatcher_marks_failed_after_max_attempts(self):
        """Test dispatcher marks delivery as dead_lettered after max attempts."""
        app = web.Application()
        mock_session = MagicMock()
        response = AsyncMock()
        response.text = AsyncMock(return_value="Error")
        response.status = 500
        mock_cm = MagicMock()
        mock_cm.__aenter__ = AsyncMock(return_value=response)
        mock_cm.__aexit__ = AsyncMock(return_value=None)
        mock_session.post.return_value = mock_cm
        app["webhook_http_session"] = mock_session

        mock_delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret=None,
            request_body={"event": "test"},
            status="pending",
            attempt_count=settings.webhook_max_attempts,
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        with patch("experiment_service.webhooks_dispatcher.get_pool") as mock_get_pool, \
             patch("experiment_service.webhooks_dispatcher.WebhookDeliveryRepository") as MockRepo:

            mock_pool = MagicMock()
            mock_get_pool.return_value = mock_pool
            
            mock_repo = MagicMock()
            mock_repo.claim_due_pending = AsyncMock(return_value=[mock_delivery])
            mock_repo.mark_attempt = AsyncMock()
            MockRepo.return_value = mock_repo

            from experiment_service.webhooks_dispatcher import _dispatcher_loop

            task = asyncio.create_task(_dispatcher_loop(app))
            await asyncio.sleep(0.1)
            task.cancel()

            try:
                await task
            except asyncio.CancelledError:
                pass

            # Verify marked as dead_lettered
            call_args = mock_repo.mark_attempt.call_args
            assert call_args.kwargs["success"] is False
            assert call_args.kwargs["status"] == "dead_lettered"

    @pytest.mark.asyncio
    async def test_dispatcher_schedules_retry_for_failed_delivery(self):
        """Test dispatcher schedules retry for failed delivery."""
        app = web.Application()
        mock_session = MagicMock()
        response = AsyncMock()
        response.text = AsyncMock(return_value="Error")
        response.status = 500
        mock_cm = MagicMock()
        mock_cm.__aenter__ = AsyncMock(return_value=response)
        mock_cm.__aexit__ = AsyncMock(return_value=None)
        mock_session.post.return_value = mock_cm
        app["webhook_http_session"] = mock_session

        mock_delivery = WebhookDelivery(
            id=uuid4(),
            subscription_id=uuid4(),
            project_id=uuid4(),
            event_type="test.event",
            target_url="http://example.com/webhook",
            secret=None,
            request_body={"event": "test"},
            status="pending",
            attempt_count=2,  # Not at max yet
            last_error=None,
            dedup_key=None,
            locked_at=None,
            next_attempt_at=datetime.now(timezone.utc),
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )

        with patch("experiment_service.webhooks_dispatcher.get_pool") as mock_get_pool, \
             patch("experiment_service.webhooks_dispatcher.WebhookDeliveryRepository") as MockRepo:

            mock_pool = MagicMock()
            mock_get_pool.return_value = mock_pool
            
            mock_repo = MagicMock()
            mock_repo.claim_due_pending = AsyncMock(return_value=[mock_delivery])
            mock_repo.mark_attempt = AsyncMock()
            mock_repo.schedule_retry = AsyncMock()
            MockRepo.return_value = mock_repo

            from experiment_service.webhooks_dispatcher import _dispatcher_loop

            task = asyncio.create_task(_dispatcher_loop(app))
            await asyncio.sleep(0.1)
            task.cancel()

            try:
                await task
            except asyncio.CancelledError:
                pass

            # Verify retry scheduled
            call_args = mock_repo.mark_attempt.call_args
            assert call_args.kwargs["success"] is False
            assert call_args.kwargs["status"] == "pending"
            assert call_args.kwargs["next_attempt_at"] is not None
