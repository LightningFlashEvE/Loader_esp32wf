FROM node:18-alpine

WORKDIR /app

# 复制package文件
COPY package*.json ./

# 安装依赖
RUN npm install --omit=dev

# 复制应用代码
COPY server.js ./
COPY public ./public

# 暴露端口
EXPOSE 3000

# 启动应用
CMD ["node", "server.js"]
