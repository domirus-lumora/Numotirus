#!/bin/bash
# 一键构建所有平台 / Build all platforms
# 需要在对应平台运行 / Run on each platform

echo "🏗️  Numotirus Multi-Platform Builder"
echo "==================================="

# 检测当前平台 / Detect current platform
OS=$(uname -s)

case "$OS" in
    Linux)
        if [ -d "/data/data/com.termux" ]; then
            echo "📱 Android/Termux detected"
            ./build_android.sh
        else
            echo "🐧 Linux detected"
            ./build.sh
        fi
        ;;
    Darwin)
        echo "🍎 macOS detected"
        ./build.sh
        echo ""
        echo "For iOS: ./build_ios.sh (requires Xcode)"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        echo "🪟 Windows detected"
        ./build.bat
        ;;
    *)
        echo "❌ Unknown OS: $OS"
        exit 1
        ;;
esac

echo "✅ Build complete!"