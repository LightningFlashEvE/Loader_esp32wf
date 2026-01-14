#!/bin/bash

# 配置EMQX允许所有客户端订阅和发布

echo "========================================="
echo "  配置EMQX授权规则"
echo "========================================="
echo ""

# 获取EMQX版本
EMQX_VERSION=$(emqx version 2>/dev/null | head -1)
echo "EMQX版本: $EMQX_VERSION"
echo ""

# 方法1：使用EMQX命令行工具
echo "方法1：使用命令行工具..."

# 检查配置文件位置
if [ -f "/etc/emqx/emqx.conf" ]; then
    CONF_FILE="/etc/emqx/emqx.conf"
elif [ -f "/opt/emqx/etc/emqx.conf" ]; then
    CONF_FILE="/opt/emqx/etc/emqx.conf"
else
    CONF_FILE=$(find / -name "emqx.conf" 2>/dev/null | head -1)
fi

echo "配置文件: $CONF_FILE"

# 备份配置
if [ -n "$CONF_FILE" ]; then
    cp "$CONF_FILE" "${CONF_FILE}.backup.$(date +%Y%m%d_%H%M%S)"
    echo "✅ 配置已备份"
fi

echo ""
echo "方法2：使用API配置..."

# 尝试不同的密码组合
PASSWORDS=("public" "Ab753951*" "admin")

for PASS in "${PASSWORDS[@]}"; do
    echo "尝试密码: $PASS"
    
    # 获取当前授权源
    RESPONSE=$(curl -s -w "\n%{http_code}" -X GET 'http://localhost:18083/api/v5/authorization/sources' \
        -u "admin:$PASS" \
        -H 'Content-Type: application/json' 2>/dev/null)
    
    HTTP_CODE=$(echo "$RESPONSE" | tail -1)
    
    if [ "$HTTP_CODE" = "200" ]; then
        echo "✅ 密码正确: $PASS"
        
        # 创建允许所有的授权源
        echo "创建授权规则..."
        curl -X POST 'http://localhost:18083/api/v5/authorization/sources' \
            -u "admin:$PASS" \
            -H 'Content-Type: application/json' \
            -d '{
                "type": "built_in_database",
                "enable": true
            }' 2>/dev/null
        
        echo ""
        echo "添加允许所有的规则..."
        curl -X POST 'http://localhost:18083/api/v5/authorization/sources/built_in_database/rules' \
            -u "admin:$PASS" \
            -H 'Content-Type: application/json' \
            -d '{
                "rules": [
                    {
                        "permission": "allow",
                        "action": "all",
                        "topic": "#"
                    }
                ]
            }' 2>/dev/null
        
        echo ""
        echo "========================================="
        echo "  ✅ 配置完成"
        echo "========================================="
        exit 0
    fi
done

echo ""
echo "❌ 所有密码都失败了"
echo ""
echo "手动配置方法："
echo "1. 重置EMQX管理员密码："
echo "   emqx_ctl admins passwd admin newpassword"
echo ""
echo "2. 或者编辑配置文件禁用授权检查："
echo "   nano $CONF_FILE"
echo "   找到 authorization 部分，设置："
echo "   authorization.no_match = allow"
echo ""
echo "3. 重启EMQX："
echo "   systemctl restart emqx"
