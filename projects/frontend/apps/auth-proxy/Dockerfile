# Build context is the repo root (see docker-compose.yml) so this Dockerfile
# can install from the shared npm workspace lockfile.
ARG APP_DIR=projects/frontend/apps/auth-proxy

# Base stage: install dependencies
FROM node:24-alpine AS base
ARG APP_DIR
WORKDIR /app

# Copy workspace manifests first for better layer caching
COPY package.json package-lock.json ./
COPY projects/frontend/tsconfig.base.json projects/frontend/tsconfig.base.json
COPY ${APP_DIR}/package.json ${APP_DIR}/package.json
RUN npm ci --workspace=auth-proxy --ignore-scripts --no-audit --no-fund

# Development stage: for hot-reload with ts-node-dev
FROM base AS development
ARG APP_DIR
ENV NODE_ENV=development

COPY ${APP_DIR}/tsconfig.json ${APP_DIR}/
COPY ${APP_DIR}/src ${APP_DIR}/src
WORKDIR /app/${APP_DIR}

EXPOSE 8080
CMD ["npm", "run", "dev"]

# Build stage: compile TypeScript
FROM base AS build
ARG APP_DIR
COPY ${APP_DIR}/tsconfig.json ${APP_DIR}/tsconfig.build.json ${APP_DIR}/
COPY ${APP_DIR}/src ${APP_DIR}/src
WORKDIR /app/${APP_DIR}
RUN npm run build

# Production stage: minimal runtime image
FROM node:24-alpine AS production
ARG APP_DIR
WORKDIR /app
ENV NODE_ENV=production

COPY package.json package-lock.json ./
COPY ${APP_DIR}/package.json ${APP_DIR}/package.json
RUN npm ci --workspace=auth-proxy --omit=dev --ignore-scripts --no-audit --no-fund

COPY --from=build /app/${APP_DIR}/dist ${APP_DIR}/dist
WORKDIR /app/${APP_DIR}

EXPOSE 8080
CMD ["node", "dist/index.js"]
