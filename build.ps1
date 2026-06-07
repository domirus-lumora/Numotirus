# build.ps1
# Numotirus 一键编译脚本 (Windows PowerShell)

$ErrorActionPreference = "Stop"

# 1. 清理并创建 build 目录
if (Test-Path build) {
    Remove-Item -Recurse -Force build
}
New-Item -ItemType Directory -Path build | Out-Null

# 2. 进入 build 目录
Push-Location build

try {
    # 3. 运行 CMake 配置 (使用 MinGW Makefiles)
    cmake .. -G "MinGW Makefiles"
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }

    # 4. 编译
    cmake --build .
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }

    # 5. 运行测试
    Write-Host "`n=== Running crypto_test ===`n" -ForegroundColor Cyan
    .\core\crypto_test.exe
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }

    Write-Host "`n✅ Build and tests succeeded!" -ForegroundColor Green
}
finally {
    Pop-Location
}