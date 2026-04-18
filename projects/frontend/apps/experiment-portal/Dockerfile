# Development stage: for hot-reload with Vite
FROM node:24-alpine AS development
WORKDIR /app

ENV NODE_ENV=development

# Copy package files
COPY package*.json ./

# Install all dependencies (including dev)
RUN npm ci --no-audit --no-fund

# Copy source code
COPY . .

EXPOSE 3000

CMD ["npm", "run", "dev"]

# Build stage: compile React app
FROM node:24-alpine AS builder

WORKDIR /app

# VITE_ vars are baked in at build time.
# Empty = use same-origin (nginx proxies /auth /api /projects → auth-proxy).
ARG VITE_AUTH_PROXY_URL=
ENV VITE_AUTH_PROXY_URL=${VITE_AUTH_PROXY_URL}

# Copy package files
COPY package*.json ./

# Install dependencies
RUN npm ci --no-audit --no-fund

# Copy source code
COPY . .

# Build
RUN npm run build

# Production stage: serve with nginx
FROM nginx:alpine AS production

# Copy built files
COPY --from=builder /app/dist /usr/share/nginx/html

# Copy nginx config
COPY nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80

CMD ["nginx", "-g", "daemon off;"]

