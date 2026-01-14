#!/bin/bash

# Mosquitto 初始化脚本

echo "初始化Mosquitto配置..."

# 创建必要的目录
mkdir -p data log

# 创建密码文件
echo "创建管理员账户..."
docker run -it --rm -v $(pwd)/config:/mosquitto/config eclipse-mosquitto:2 \
    mosquitto_passwd -c /mosquitto/config/passwd admin

echo ""
echo "✅ 配置完成"
echo ""
echo "启动Mosquitto:"
echo "  docker-compose up -d mosquitto"
echo ""
echo "测试连接:"
echo "  mosquitto_sub -h localhost -p 1883 -u admin -P admin -t '#' -v"
