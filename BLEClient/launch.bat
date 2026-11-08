@echo off
chcp 65001 >nul

echo ==============================
echo 检查项目依赖...
echo ==============================
pip install -r requirements.txt --quiet
if %errorlevel% neq 0 (
    echo 警告：部分依赖安装失败，请手动检查 requirements.txt
) else (
    echo 依赖检查/安装完成！
)

echo ==============================
echo 正在启动应用程序...
echo ==============================

:: 切换到脚本所在目录（项目根目录）
cd /d %~dp0

:: 启动应用程序
python ble_app.py

:: 保持窗口打开
pause
