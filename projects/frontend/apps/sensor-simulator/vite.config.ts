/// <reference types="vitest" />
import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
    plugins: [react()],
    server: {
        port: 3006,
        strictPort: true,
        proxy: {
            // Same path prefix as in production nginx.conf
            '/telemetry': {
                target: 'http://telemetry-ingest-service:8003',
                changeOrigin: true,
                secure: false,
                // Strip prefix so:
                //   /telemetry/api/v1/telemetry -> /api/v1/telemetry
                //   /telemetry/api/v1/telemetry/stream -> /api/v1/telemetry/stream
                rewrite: (path) => path.replace(/^\/telemetry/, ''),
            },
        },
    },
    test: {
        environment: 'jsdom',
        globals: true,
        setupFiles: ['./src/setupTests.ts'],
        css: false,
        coverage: {
            provider: 'v8',
            reporter: ['text', 'html'],
            include: ['src/**/*.{ts,tsx}'],
            exclude: ['src/main.tsx', 'src/setupTests.ts', '**/*.test.{ts,tsx}'],
            // Ratchet floor — sensor-simulator is smoke-only.
            // Plan target: 50% lines.
            // Measured baseline (CI run 25581222556): 52.2% lines, 49.68%
            // statements, 58.33% funcs, 46.02% branches.
            thresholds: {
                lines: 50,
                statements: 47,
                functions: 50,
                branches: 44,
            },
        },
    },
})

