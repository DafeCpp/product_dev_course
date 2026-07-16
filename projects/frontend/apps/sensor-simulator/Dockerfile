# Build context is the repository root so the simulator can import
# the shared frontend-common telemetry stream hook.
ARG APP_DIR=projects/frontend/apps/sensor-simulator

# Development stage: for hot-reload with Vite
FROM node:24-alpine AS development
ARG APP_DIR
WORKDIR /app/${APP_DIR}

ENV NODE_ENV=development

# Install application dependencies before copying source for cache reuse.
COPY ${APP_DIR}/package*.json ./
RUN npm ci --no-audit --no-fund

COPY projects/frontend/common /app/projects/frontend/common
COPY ${APP_DIR} .
RUN ln -s /app/${APP_DIR}/node_modules /app/projects/frontend/common/node_modules

EXPOSE 3006
CMD ["npm", "run", "dev", "--", "--host", "0.0.0.0", "--port", "3006", "--strictPort"]

# Build stage: compile React app
FROM node:24-alpine AS builder
ARG APP_DIR
WORKDIR /app/${APP_DIR}

COPY ${APP_DIR}/package*.json ./
RUN npm ci --no-audit --no-fund

COPY projects/frontend/common /app/projects/frontend/common
COPY ${APP_DIR} .
RUN ln -s /app/${APP_DIR}/node_modules /app/projects/frontend/common/node_modules
RUN npm run build

# Production stage: serve with nginx
FROM nginx:alpine AS production
ARG APP_DIR

COPY --from=builder /app/${APP_DIR}/dist /usr/share/nginx/html
COPY ${APP_DIR}/nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]
