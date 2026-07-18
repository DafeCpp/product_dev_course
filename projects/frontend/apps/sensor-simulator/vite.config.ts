/// <reference types="vitest" />
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { fileURLToPath } from "node:url";

const reactPath = fileURLToPath(new URL("./node_modules/react", import.meta.url));
const reactDomPath = fileURLToPath(new URL("./node_modules/react-dom", import.meta.url));

export default defineConfig({
  plugins: [react()],
  resolve: {
    // frontend-common is consumed from its source tree; force one React
    // instance so hooks shared across workspaces use the app dispatcher.
    dedupe: ["react", "react-dom"],
    alias: [
      { find: /^react$/, replacement: reactPath },
      { find: /^react\/(.+)$/, replacement: `${reactPath}/$1` },
      { find: /^react-dom$/, replacement: reactDomPath },
      { find: /^react-dom\/(.+)$/, replacement: `${reactDomPath}/$1` },
    ],
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
      // Initial ratchet from Coverage Baseline run 29654381568.
      thresholds: process.env.COVERAGE_ENFORCE_RATCHET === "true" ? {
        lines: 81.73,
        statements: 47,
        functions: 50,
        branches: 78.28,
      } : undefined,
    },
  },
});
