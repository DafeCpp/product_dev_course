FROM node:20-alpine AS base
WORKDIR /app

COPY package*.json ./
RUN npm install --ignore-scripts

COPY tsconfig.json ./
COPY src ./src

RUN npm run build

FROM node:20-alpine
WORKDIR /app
ENV NODE_ENV=production

COPY --from=base /app/package*.json ./
RUN npm install --omit=dev --ignore-scripts

COPY --from=base /app/dist ./dist

EXPOSE 8080
CMD ["node", "dist/index.js"]

