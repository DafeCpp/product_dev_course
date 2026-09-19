# P10-09 Projects HTTP pilot

Only `projectsApi` adopts `@lostpointer/web-http`. Other domains and `usersApi`
keep their existing clients. Session recovery was extracted, without changing
its ownership, into `api/http/session.ts`; both clients register the same local
policy. The migrated client's `configure` hook installs refresh before final
error normalization. Trace ID, CSRF cookie reading, the 30-second timeout,
credentialed requests and project parameters remain application-owned.

Projects callers now receive a normalized `HttpError` on final failure, with a
readable message and original Axios error in `cause`. Existing project UI error
handlers already fall back to `message`. Refresh still sees raw Axios errors,
skips sensor-token requests, and bounds retry with `_retry`.

Validation on Node 24:

- API suite: 151 tests passed, including eight shared-client/recovery tests.
- Full Experiment Portal suite: 548 passed, two existing skips (`TZ=UTC`).
  Existing RunsList date assertions depend on UTC.
- Type check and production build passed.

## Release gate

The adoption branch is a draft while its package reference points at a local
validation tarball. Do not merge it until web-platform publishes version 0.1.0.
From the repository root, resolve the real public release and regenerate locks:

```sh
npm install -w projects/frontend/apps/experiment-portal --save-exact @lostpointer/web-http@0.1.0
npm ci
TZ=UTC npm test -w projects/frontend/apps/experiment-portal
npm run type-check -w projects/frontend/apps/experiment-portal
npm run build -w projects/frontend/apps/experiment-portal
```

Commit the manifest and root lockfile with the registry dependency. No local
package paths belong in the final adoption. Other domains can migrate through
the same adapter in later changes, with their own contract/recovery tests.
Rollback reverts the pilot; the legacy client remains available.
