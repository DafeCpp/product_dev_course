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

## Published dependency

`@lostpointer/web-react@0.1.1` was published publicly on 2026-09-18 UTC
from the reviewed tarball. Registry integrity matches the tested artifact.
The root workspace lockfile now resolves the npm release; no local file
dependency is committed. Clean npm ci, type-check, production build and
coverage tests with COVERAGE_ENFORCE_RATCHET=true passed against the registry
package: 540 tests passed, 2 existing skipped. Coverage: statements 57.24%,
branches 50.18%, functions 50.77%, lines 59.8%.

The earlier PR #343 install failure was an npm E404 before publication.
Future automated releases still require repairing the web-platform release
workflow credentials (its previous npm whoami failed with E401).
The full backend Cypress lifecycle suite remains a separate validation.
