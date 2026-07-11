FROM node:20-slim
WORKDIR /app
RUN apt-get update && apt-get install -y --no-install-recommends curl \
    && rm -rf /var/lib/apt/lists/*
COPY package.json ./
RUN npm install --omit=dev && npm cache clean --force
COPY server.js ./
USER node
EXPOSE 8081
HEALTHCHECK --interval=10s --timeout=2s --retries=3 \
  CMD curl -fsS http://localhost:8081/health || exit 1
CMD ["node", "server.js"]
