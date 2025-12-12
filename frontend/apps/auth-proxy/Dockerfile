# Base stage: install dependencies
FROM node:20-alpine AS base
WORKDIR /app

COPY package*.json ./
RUN npm install --ignore-scripts

# Development stage: for hot-reload with ts-node-dev
FROM base AS development
ENV NODE_ENV=development

COPY tsconfig.json ./
COPY src ./src

EXPOSE 8080
CMD ["npm", "run", "dev"]

# Build stage: compile TypeScript
FROM base AS build
COPY tsconfig.json ./
COPY src ./src
RUN npm run build

# Production stage: minimal runtime image
FROM node:20-alpine AS production
WORKDIR /app
ENV NODE_ENV=production

COPY --from=build /app/package*.json ./
RUN npm install --omit=dev --ignore-scripts

COPY --from=build /app/dist ./dist

EXPOSE 8080
CMD ["node", "dist/index.js"]

