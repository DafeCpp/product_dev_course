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
      // Ratchet from the LOS-203 coverage run (v8 provider): lines 97.96,
      // statements 97.32, functions 98.07, branches 92.01.
      thresholds: process.env.COVERAGE_ENFORCE_RATCHET === 'true' ? {
        lines: 97,
        statements: 96,
        functions: 97,
        branches: 91,
      } : undefined,
    },
  },
})
