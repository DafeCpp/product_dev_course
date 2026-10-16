# Build context is the repository root so the simulator can install from the
# shared npm workspace lockfile and import the frontend-common telemetry hook.
ARG APP_DIR=projects/frontend/apps/sensor-simulator

# Development stage: for hot-reload with Vite
FROM node:24-alpine AS development
ARG APP_DIR
WORKDIR /app

ENV NODE_ENV=development

# Copy workspace manifests first for better layer caching
COPY package.json package-lock.json ./
COPY projects/frontend/tsconfig.base.json projects/frontend/tsconfig.react.json projects/frontend/
COPY projects/frontend/common/package.json projects/frontend/common/package.json
COPY ${APP_DIR}/package.json ${APP_DIR}/package.json
RUN npm ci --workspace=sensor-simulator --no-audit --no-fund

COPY projects/frontend/common projects/frontend/common
COPY ${APP_DIR} ${APP_DIR}
WORKDIR /app/${APP_DIR}

EXPOSE 3006
CMD ["npm", "run", "dev", "--", "--host", "0.0.0.0", "--port", "3006", "--strictPort"]

# Build stage: compile React app
FROM node:24-alpine AS builder
ARG APP_DIR
WORKDIR /app

COPY package.json package-lock.json ./
COPY projects/frontend/tsconfig.base.json projects/frontend/tsconfig.react.json projects/frontend/
COPY projects/frontend/common/package.json projects/frontend/common/package.json
COPY ${APP_DIR}/package.json ${APP_DIR}/package.json
RUN npm ci --workspace=sensor-simulator --no-audit --no-fund

COPY projects/frontend/common projects/frontend/common
COPY ${APP_DIR} ${APP_DIR}
WORKDIR /app/${APP_DIR}
RUN npm run build

# Production stage: serve with nginx
FROM nginx:alpine AS production
ARG APP_DIR

COPY --from=builder /app/${APP_DIR}/dist /usr/share/nginx/html
COPY ${APP_DIR}/nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]
