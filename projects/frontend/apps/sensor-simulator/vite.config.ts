/// <reference types="vitest" />
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  resolve: {
    // frontend-common is consumed from its source tree; force one React
    // instance so hooks shared across workspaces use the app dispatcher.
    dedupe: ["react", "react-dom"],
  },
  server: {
    port: 3006,
    strictPort: true,
    proxy: {
      // Same path prefix as in production nginx.conf
      "/telemetry": {
        target: "http://telemetry-ingest-service:8003",
        changeOrigin: true,
        secure: false,
        // Strip prefix so:
        //   /telemetry/api/v1/telemetry -> /api/v1/telemetry
        //   /telemetry/api/v1/telemetry/stream -> /api/v1/telemetry/stream
        rewrite: (path) => path.replace(/^\/telemetry/, ""),
      },
    },
  },
  test: {
    environment: "jsdom",
    globals: true,
    setupFiles: ["./src/setupTests.ts"],
    css: false,
    coverage: {
      provider: "v8",
      reporter: ["text", "html", "lcov", "json-summary", "cobertura"],
      include: ["src/**/*.{ts,tsx}"],
      exclude: ["src/main.tsx", "src/setupTests.ts", "**/*.test.{ts,tsx}"],
      // Ratchet from the LOS-205 coverage run: lines 98.08, statements 97.71,
      // functions 98.31, branches 95.47.
      thresholds: process.env.COVERAGE_ENFORCE_RATCHET === "true" ? {
        lines: 97,
        statements: 96,
        functions: 97,
        branches: 94,
      } : undefined,
    },
  },
});
