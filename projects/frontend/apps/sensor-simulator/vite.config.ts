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
})

