# P10-08: shared React primitives

Experiment Portal consumes `@lostpointer/web-react@0.1.1` through its existing
common adapters. PageHeader is used by CreateSensor; EmptyState, Loading,
ErrorState and FormActions are used by lists and forms. ProjectModal also
uses shared FormActions, preserving its submit/cancel/disabled behavior.
ProtectedRoute keeps its authentication policy and delegates only loading UI.

Russian defaults, application DTO, status maps, API calls, permissions and
routing remain local. Shared CSS is imported once after web-styles.
Unused per-component SCSS is removed; global legacy styles still used by
other components are retained.

## Validation

Validated against a locally packed web-platform commit
`33faea68e9492e1ce7610549a09621071b4a61fe` (manifest 0.1.1):

- Shared repository: full pnpm check, 69 tests and Storybook build.
- Portal: 540 tests passed, 2 existing skipped; type-check and production build.
- On the available Node 25 runtime, tests require
  `TZ=UTC NODE_OPTIONS=--no-experimental-webstorage npm run test --workspace experiment-tracking-frontend`.
  The intended project runtime is Node 24.
- Cypress: login.cy.ts (2 tests) and shared_primitives.cy.ts (3 tests).
  The latter intercepts API responses and covers actual pages at 390/1440px,
  loading/empty/error states, sensor page header and project form actions.
  This does not replace backend lifecycle tests.

## Release dependency — keep this PR draft

The npm registry currently returns E404 for web-react. Release workflow run
32781301900 in LostPointer/web-platform failed at npm whoami (E401).
Publishing is intentionally deferred until after this implementation.

The manifest pins the intended registry version, but package-lock.json must
be regenerated after publication. No local tarball URL or fabricated registry
integrity is committed. Therefore a clean npm ci cannot yet validate this draft.

Before merge: restore publishing, publish the reviewed version, regenerate
the root workspace lockfile, run clean npm ci and repeat the checks against
the registry artifact; run the full backend Cypress lifecycle suite separately.
