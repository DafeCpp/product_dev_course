"""Email sending service."""
from __future__ import annotations

import email.mime.multipart
import email.mime.text
from datetime import datetime

import aiosmtplib
import structlog

from auth_service.settings import Settings

logger = structlog.get_logger(__name__)


class EmailService:
    def __init__(self, settings: Settings) -> None:
        self._settings = settings

    async def send_password_reset_email(
        self,
        to_email: str,
        token: str,
        expires_at: datetime,
    ) -> None:
        if not self._settings.smtp_enabled:
            logger.info("smtp_disabled_skip_email", to=to_email)
            return

        reset_url = f"{self._settings.app_url}/reset-password?token={token}"
        expires_str = expires_at.strftime("%d.%m.%Y %H:%M UTC")

        html = _build_reset_html(reset_url, expires_str)
        text = _build_reset_text(reset_url, expires_str)

        msg = email.mime.multipart.MIMEMultipart("alternative")
        msg["Subject"] = "Сброс пароля — Experiment Platform"
        msg["From"] = f"{self._settings.smtp_from_name} <{self._settings.smtp_from}>"
        msg["To"] = to_email
        msg.attach(email.mime.text.MIMEText(text, "plain", "utf-8"))
        msg.attach(email.mime.text.MIMEText(html, "html", "utf-8"))

        try:
            kwargs: dict[str, object] = dict(
                message=msg,
                hostname=self._settings.smtp_host,
                port=self._settings.smtp_port,
                timeout=10,
            )
            if self._settings.smtp_user:
                kwargs["username"] = self._settings.smtp_user
                kwargs["password"] = self._settings.smtp_password
            await aiosmtplib.send(**kwargs)  # type: ignore[arg-type,call-arg]
            logger.info("password_reset_email_sent", to=to_email)
        except Exception:
            logger.exception("password_reset_email_failed", to=to_email)


def _build_reset_html(reset_url: str, expires_str: str) -> str:
    return f"""<!DOCTYPE html>
<html>
<head><meta charset="utf-8"></head>
<body style="font-family:sans-serif;background:#f5f5f5;margin:0;padding:20px">
  <div style="max-width:480px;margin:0 auto;background:#fff;border-radius:8px;padding:32px;border:1px solid #e0e0e0">
    <h2 style="color:#1976d2;margin-top:0">Сброс пароля</h2>
    <p>Вы запросили сброс пароля для вашего аккаунта. Нажмите кнопку ниже для продолжения:</p>
    <p style="text-align:center;margin:28px 0">
      <a href="{reset_url}"
         style="display:inline-block;background:#1976d2;color:#fff;text-decoration:none;padding:12px 28px;border-radius:6px;font-weight:600">
        Сбросить пароль
      </a>
    </p>
    <p style="color:#757575;font-size:0.9em">Ссылка действительна до {expires_str}.</p>
    <p style="color:#757575;font-size:0.9em">Если вы не запрашивали сброс пароля, проигнорируйте это письмо.</p>
    <hr style="border:none;border-top:1px solid #e0e0e0;margin:24px 0">
    <p style="color:#9e9e9e;font-size:0.8em">Experiment Platform</p>
  </div>
</body>
</html>"""


def _build_reset_text(reset_url: str, expires_str: str) -> str:
    return (
        f"Сброс пароля\n\n"
        f"Для сброса пароля перейдите по ссылке:\n{reset_url}\n\n"
        f"Ссылка действительна до {expires_str}.\n\n"
        f"Если вы не запрашивали сброс пароля, проигнорируйте это письмо."
    )
