param(
    # 工程根目录。不传的话就按脚本自身的位置推出来。
    [string]$Root = ''
)

Add-Type -AssemblyName System.Drawing

# 把 tests\test-output.txt 里那些用 '#' / '.' 打出来的 128x64 显存转成 PNG。
#
# 早期版本这里写死了 F:\Stm32\tests\...，那是工程还在上一级目录时留下的
# 路径，工程搬进 Project\ 之后就再也找不到文件了。现在按脚本自身位置推导，
# 从哪里调用都不会错。
if (-not $Root) {
    # $PSScriptRoot 在某些调用方式下会是空的，所以再兜一层
    $here = $PSScriptRoot
    if (-not $here) { $here = Split-Path -Parent $MyInvocation.MyCommand.Definition }
    $Root = Split-Path -Parent $here
}
if (-not $Root -or -not (Test-Path (Join-Path $Root 'tests'))) {
    throw "renderscreens.ps1: 找不到工程目录（Root='$Root'）"
}

$lines = Get-Content (Join-Path $Root 'tests\test-output.txt')
$scale = 5
$outDir = Join-Path $Root 'tests\screens'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# 先清掉上一次渲染出来的图，否则改名之后会留下过期的旧文件
Remove-Item (Join-Path $outDir '*.png') -ErrorAction SilentlyContinue

$shot = 0
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -notmatch '^==== ') { continue }
    $title = $lines[$i]
    $rows = @()
    for ($k = 1; $k -le 64; $k++) {
        $l = $lines[$i + $k]
        if ($l -notmatch '^[#.]{128}$') { break }
        $rows += $l
    }
    if ($rows.Count -ne 64) { continue }

    $shot++
    $bmp = New-Object System.Drawing.Bitmap (128 * $scale), (64 * $scale)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0))
    $on = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(180, 230, 255))
    for ($y = 0; $y -lt 64; $y++) {
        for ($x = 0; $x -lt 128; $x++) {
            if ($rows[$y][$x] -eq '#') {
                $g.FillRectangle($on, $x * $scale, $y * $scale, $scale, $scale)
            }
        }
    }
    $g.Dispose()
    $name = Join-Path $outDir ("{0:d2}.png" -f $shot)
    $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output ("{0}  <= {1}" -f $name, $title)
}
