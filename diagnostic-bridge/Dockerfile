FROM node:22-alpine
ENV NODE_ENV=production
WORKDIR /app
COPY --chown=node:node server.mjs ./
USER node
EXPOSE 8787
CMD ["node", "server.mjs"]
