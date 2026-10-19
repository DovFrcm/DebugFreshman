# run_tests.ps1 - 在电脑上编译并运行全部测试，然后把画面渲染成 PNG
#
# 不需要开发板。它把固件里真实的 oled.c / oled_font.c / menu.c / app.c /
# track.c / motor.c 编译到电脑上，只把硬件层（board.c、serial.c）换成
# tests\ 下的内存替身，所以测的是真代码，不是另写一份。
#
# 用法（需要 MinGW 的 gcc 在 PATH 里）：
#     powershell -File tools\run_tests.ps1
#
# 退出码 0 表示全部通过。

$ErrorActionPreference = 'Stop'

# 工程根目录。$PSScriptRoot 在某些调用方式下会是空的，所以再兜一层。
$here = $PSScriptRoot
if (-not $here) { $here = Split-Path -Parent $MyInvocation.MyCommand.Definition }
$root = Split-Path -Parent $here          # F:\Stm32\Project
Set-Location $root

$common = @(
    '-std=c99', '-Wall', '-Wextra', '-O1',
    '-I', 'User', '-include', 'tests/fake_board.h'
)
$fw = @(
    'User/oled.c', 'User/oled_font.c', 'User/menu.c',
    'User/app.c', 'User/track.c', 'User/motor.c'
)
$fake = @('tests/fake_board.c', 'tests/fake_serial.c')

$failed = 0
$log = New-Object System.Text.StringBuilder

function Invoke-Suite {
    param([string]$Name, [string]$Main)

    Write-Host "=== building $Name ==="
    $exe = "tests/$Name.exe"
    $src = @("tests/$Main") + $fake + $fw

    & gcc @common @src -o $exe
    if ($LASTEXITCODE -ne 0) {
        Write-Host "BUILD FAILED: $Name" -ForegroundColor Red
        $script:failed = 1
        return
    }

    Write-Host "=== running $Name ==="
    $out = & ".\$exe" 2>&1 | Out-String
    [void]$script:log.AppendLine($out)
    Write-Host $out

    if ($LASTEXITCODE -ne 0) {
        Write-Host "TESTS FAILED: $Name" -ForegroundColor Red
        $script:failed = 1
    }
}

Invoke-Suite -Name 'test_menu'  -Main 'test_menu.c'
Invoke-Suite -Name 'test_track' -Main 'test_track.c'

# 把两个套件的输出合起来存盘，renderscreens.ps1 会把它渲染成 PNG。
# 用 UTF8 无 BOM 写，否则中文会变成乱码。
# （New-Object 要先赋给变量：Windows PowerShell 5.1 不允许它直接写在
#   方法调用的参数位置上。）
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText(
    (Join-Path $root 'tests\test-output.txt'),
    $log.ToString(),
    $utf8NoBom)

Write-Host "=== rendering screen shots ==="
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'renderscreens.ps1') -Root $root

if ($failed -ne 0) {
    Write-Host 'RESULT: FAILURES' -ForegroundColor Red
    exit 1
}
Write-Host 'RESULT: all tests passed' -ForegroundColor Green
exit 0
