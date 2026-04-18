# ADR-004: Refresh Token Rotation with Reuse Detection

**Status:** Accepted
**Date:** 2026-03-19

## Context

Current `POST /auth/refresh` endpoint validates the refresh token and issues new access + refresh tokens, but does **not** revoke the old refresh token. This means:

1. A stolen refresh token can be used indefinitely until it expires (14 days by default).
2. There is no mechanism to detect token theft -- if an attacker replays a refresh token, the legitimate user and attacker both continue to work.
3. The `revoked_tokens` table grows indefinitely; there is no cleanup of expired entries.

Industry best practice (OWASP, RFC 6749 Section 10.4) recommends **refresh token rotation**: each use of a refresh token invalidates it and issues a new one. If a revoked token is reused, the entire token chain (family) must be invalidated, forcing re-authentication.

## Decision

### 1. Database changes

#### New table: `refresh_token_families`

```sql
CREATE TABLE refresh_token_families (
    id         uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id    uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at timestamptz NOT NULL DEFAULT now(),
    revoked_at timestamptz  -- NULL = active; non-NULL = entire family revoked
);

CREATE INDEX refresh_token_families_user_id_idx ON refresh_token_families (user_id);
CREATE INDEX refresh_token_families_revoked_at_idx ON refresh_token_families (revoked_at) WHERE revoked_at IS NULL;
```

Purpose: groups a chain of refresh tokens. When a login/register/password-reset occurs, a new family is created. All subsequent refreshes within that session belong to the same family.

#### Alter `revoked_tokens`: add `family_id`

```sql
ALTER TABLE revoked_tokens
    ADD COLUMN family_id uuid REFERENCES refresh_token_families(id) ON DELETE CASCADE;

CREATE INDEX revoked_tokens_family_id_idx ON revoked_tokens (family_id);
```

Existing rows will have `family_id = NULL` (pre-rotation tokens, harmless).

Migration file: `002_refresh_token_rotation.sql`

### 2. JWT claims change

Add `fid` (family ID) claim to refresh tokens:

```python
def create_refresh_token(user_id: str, family_id: str) -> str:
    payload = {
        "sub": user_id,
        "type": "refresh",
        "jti": str(uuid.uuid4()),
        "fid": family_id,          # <-- new
        "iat": now,
        "exp": now + settings.refresh_token_ttl_sec,
    }
```

Access tokens are unchanged.

### 3. Refresh flow (`AuthService.refresh_token`)

```
1. Decode refresh token -> extract jti, sub (user_id), fid (family_id)
2. Check revoked_tokens for jti:
   a. If REVOKED -> reuse detected!
      - Revoke entire family: UPDATE refresh_token_families SET revoked_at = now() WHERE id = fid
      - Audit log: "auth.token_reuse_detected"
      - Return 401 {"error": "Token reuse detected", "code": "TOKEN_REUSE"}
   b. If NOT revoked -> proceed
3. Validate user exists and is active
4. Check family is not revoked: SELECT revoked_at FROM refresh_token_families WHERE id = fid
   - If family revoked -> return 401
5. Revoke current token: INSERT INTO revoked_tokens (jti, user_id, expires_at, family_id)
6. Issue new tokens with same family_id:
   - new access_token (unchanged logic)
   - new refresh_token (new jti, same fid)
7. Return both tokens
```

### 4. Login / Register / Password Reset

These entry points create a **new family**:

```python
async def _create_tokens(self, user_id: str) -> AuthTokensResponse:
    # Create new token family
    family_id = await self._token_family_repo.create(UUID(user_id))
    return AuthTokensResponse(
        access_token=create_access_token(user_id, ...),
        refresh_token=create_refresh_token(user_id, str(family_id)),
    )
```

### 5. Logout

Unchanged semantically -- revokes the refresh token by jti. Additionally extracts `fid` and stores `family_id` in `revoked_tokens`.

### 6. Auth Proxy changes

The auth proxy (`/auth/refresh` handler and `_refreshAccessTokenForRequest`) already handles the happy path correctly:

- `setAuthCookies(reply, config, data)` updates **both** cookies when auth-service returns new tokens.
- No proxy-level changes needed for rotation itself.

Changes needed:

1. **Reuse detection handling**: When `POST /auth/refresh` returns 401 with `{"code": "TOKEN_REUSE"}`, the proxy must `clearAuthCookies()` (already happens on `!res.ok`). Optionally, add a `X-Auth-Event: token-reuse` header so the frontend can show a security warning.

2. **Logout fix**: Currently the proxy sends an empty body `{}` to `/auth/logout`. It must send `{"refresh_token": <cookie_value>}`:

```typescript
app.post('/auth/logout', async (request, reply) => {
    const refreshToken = request.cookies[config.refreshCookieName]
    // ...
    body: JSON.stringify({ refresh_token: refreshToken }),
})
```

### 7. Cleanup background task

A periodic task (registered as aiohttp `on_startup` background coroutine) deletes expired entries:

```python
async def cleanup_expired_tokens(app: web.Application) -> None:
    """Delete revoked tokens past their expiry and fully-revoked families."""
    while True:
        try:
            await asyncio.sleep(3600)  # every hour
            pool = await get_pool()
            # Delete expired revoked tokens
            await pool.execute(
                "DELETE FROM revoked_tokens WHERE expires_at < now()"
            )
            # Delete families that are revoked AND have no remaining un-expired tokens
            await pool.execute("""
                DELETE FROM refresh_token_families f
                WHERE f.revoked_at IS NOT NULL
                  AND NOT EXISTS (
                      SELECT 1 FROM revoked_tokens rt
                      WHERE rt.family_id = f.id AND rt.expires_at >= now()
                  )
            """)
        except asyncio.CancelledError:
            break
        except Exception:
            logger.exception("Token cleanup failed")
```

### 8. New repository: `TokenFamilyRepository`

File: `auth_service/repositories/token_families.py`

Methods:
- `create(user_id: UUID) -> UUID` -- creates family, returns id
- `is_revoked(family_id: UUID) -> bool` -- checks `revoked_at IS NOT NULL`
- `revoke_family(family_id: UUID) -> None` -- sets `revoked_at = now()`
- `revoke_all_user_families(user_id: UUID) -> None` -- for password change / account deactivation

### 9. `RevokedTokenRepository` changes

- `revoke()` signature gains `family_id: UUID | None = None` parameter
- No other changes; `is_revoked()` stays the same

## File change summary

| File | Change |
|------|--------|
| `migrations/002_refresh_token_rotation.sql` | New migration: `refresh_token_families` table, alter `revoked_tokens` |
| `repositories/token_families.py` | New repository |
| `repositories/revoked_tokens.py` | Add `family_id` param to `revoke()` |
| `services/jwt.py` | `create_refresh_token` gains `family_id` param |
| `services/auth.py` | Rewrite `refresh_token()`, update `_create_tokens()`, `logout()` |
| `domain/models.py` | Add `AuditAction.TOKEN_REUSE_DETECTED` |
| `main.py` | Register cleanup background task |
| `api/routes/auth.py` | Update `get_auth_service()` to inject `TokenFamilyRepository` |
| `settings.py` | Add `token_cleanup_interval_sec: int = 3600` |
| `auth-proxy/src/index.ts` | Fix logout body, optional reuse-detection header |

## Edge cases

1. **Race condition: two concurrent refreshes with the same token.** Mitigation: `INSERT INTO revoked_tokens ... ON CONFLICT (jti) DO NOTHING` + `SELECT`-after-insert check. The first writer wins; the second sees the token as already revoked and triggers reuse detection. This is acceptable -- the user will need to re-login once, which is the correct security behavior.

2. **Family-less tokens (pre-migration).** Tokens without `fid` claim: treat `fid=None` as a legacy token. Allow one refresh (issue new token WITH family), then normal rotation applies. During transition only.

3. **Password change / account deactivation.** Must call `revoke_all_user_families(user_id)` to invalidate all sessions. This is already partially handled (password change does not revoke tokens currently -- this should be added).

4. **Clock skew.** `decode_token` uses PyJWT which has a default leeway of 0. Not a concern for rotation logic since we check revocation status, not timing.

5. **Cleanup deletes a token that is being checked concurrently.** Not a problem: if the token's `expires_at` is past, PyJWT `decode_token()` will reject it with `ExpiredSignatureError` before we even check `revoked_tokens`.

## Consequences

**Positive:**
- Stolen refresh tokens become single-use; reuse is detected and triggers full session invalidation.
- Token cleanup prevents unbounded growth of `revoked_tokens`.
- Aligns with OWASP token security recommendations.

**Negative:**
- Slightly more complex refresh flow (one extra DB read + one extra DB write per refresh).
- Concurrent refresh of the same token (e.g. multiple browser tabs) will trigger false-positive reuse detection. Mitigation: the frontend must serialize refresh requests (already the case with auth-proxy acting as single BFF).
- Migration required on existing deployments (non-breaking: new column is nullable).

**Neutral:**
- Auth proxy changes are minimal (logout body fix is a bug fix regardless of this ADR).
- No API contract changes for consumers -- `POST /auth/refresh` request/response shapes are identical.
