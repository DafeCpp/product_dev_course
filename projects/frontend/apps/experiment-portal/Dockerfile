# Build context is the repo root (see docker-compose.yml) so this Dockerfile
# can reach the sibling npm workspace package at projects/frontend/common.
ARG APP_DIR=projects/frontend/apps/experiment-portal

# Development stage: for hot-reload with Vite
FROM node:24-alpine AS development
ARG APP_DIR
WORKDIR /app

ENV NODE_ENV=development

# Copy workspace manifests first for better layer caching
COPY package.json package-lock.json ./
COPY projects/frontend/common/package.json projects/frontend/common/package.json
COPY ${APP_DIR}/package.json ${APP_DIR}/package.json

# Install all dependencies (including dev) for the whole workspace
RUN npm ci --no-audit --no-fund

# Copy source code
COPY projects/frontend/common projects/frontend/common
COPY ${APP_DIR} ${APP_DIR}

WORKDIR /app/${APP_DIR}

EXPOSE 3000

CMD ["npm", "run", "dev"]

# Build stage: compile React app
FROM node:24-alpine AS builder
ARG APP_DIR
WORKDIR /app

# VITE_ vars are baked in at build time.
# Empty = use same-origin (nginx proxies /auth /api /projects → auth-proxy).
ARG VITE_AUTH_PROXY_URL=
ENV VITE_AUTH_PROXY_URL=${VITE_AUTH_PROXY_URL}

# Copy workspace manifests first for better layer caching
COPY package.json package-lock.json ./
COPY projects/frontend/common/package.json projects/frontend/common/package.json
COPY ${APP_DIR}/package.json ${APP_DIR}/package.json

# Install dependencies for the whole workspace
RUN npm ci --no-audit --no-fund

# Copy source code
COPY projects/frontend/common projects/frontend/common
COPY ${APP_DIR} ${APP_DIR}

WORKDIR /app/${APP_DIR}

# Build
RUN npm run build

# Production stage: serve with nginx
FROM nginx:alpine AS production
ARG APP_DIR

# Copy built files
COPY --from=builder /app/${APP_DIR}/dist /usr/share/nginx/html

# Copy nginx config
COPY ${APP_DIR}/nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80

CMD ["nginx", "-g", "daemon off;"]
