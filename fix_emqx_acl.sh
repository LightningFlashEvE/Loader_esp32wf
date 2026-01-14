#!/bin/bash

# 临时禁用EMQX授权（测试用）

echo "配置EMQX允许所有客户端..."

# 使用EMQX API创建允许所有的授权规则
curl -X POST 'http://localhost:18083/api/v5/authorization/sources' \
  -u admin:public \
  -H 'Content-Type: application/json' \
  -d '{
    "type": "built_in_database",
    "enable": true,
    "rules": [
      {
        "permission": "allow",
        "action": "all",
        "topic": "#"
      }
    ]
  }'

echo ""
echo "✅ 配置完成"
echo ""
echo "现在ESP32应该能接收消息了"
echo "测试命令："
echo "mosquitto_pub -h 8.135.238.216 -p 1883 -u admin -P admin -t 'dev/C3-3C8A1FB6DA20/down/epd' -m '{\"cmd\":\"EPD\",\"type\":0}'"
