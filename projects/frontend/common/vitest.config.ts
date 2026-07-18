import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

const enforceCoverageRatchet = process.env.COVERAGE_ENFORCE_RATCHET === 'true'

export default defineConfig({
  plugins: [react()],
  test: {
    globals: true,
    environment: 'jsdom',
    setupFiles: './src/setupTests.ts',
    reporters: process.env.CI ? ['verbose'] : ['default'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'html', 'lcov', 'json-summary', 'cobertura'],
      reportsDirectory: './coverage',
      thresholds: enforceCoverageRatchet
        ? {
            lines: 94,
            branches: 82,
          }
        : undefined,
      include: ['src/**/*.{ts,tsx}'],
      exclude: [
        'src/**/*.test.{ts,tsx}',
        'src/setupTests.ts',
        'src/testUtils/**',
      ],
    },
  },
})
