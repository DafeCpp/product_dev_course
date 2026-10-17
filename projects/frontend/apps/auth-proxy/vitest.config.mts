import { defineConfig } from 'vitest/config'

export default defineConfig({
  test: {
    globals: true,
    environment: 'node',
    include: ['test/**/*.test.ts'],
    clearMocks: true,
    restoreMocks: true,
    reporters: process.env.CI ? ['verbose'] : ['default'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'html', 'lcov', 'json-summary', 'cobertura'],
      reportsDirectory: './coverage',
      include: ['src/**/*.ts'],
      exclude: ['src/**/*.d.ts', 'src/**/__mocks__/**'],
      // Ratchet from Coverage Baseline run 29654381568, re-measured with the
      // v8 provider after the Jest -> Vitest switch (same 53 tests): v8 counts
      // lines differently from Jest's istanbul, so lines read 66.85 instead of
      // 69.52 and branches 60.34 instead of 59.55.
      thresholds: process.env.COVERAGE_ENFORCE_RATCHET === 'true' ? {
        lines: 66.85,
        statements: 58,
        functions: 55,
        branches: 60.34,
      } : undefined,
    },
  },
})
