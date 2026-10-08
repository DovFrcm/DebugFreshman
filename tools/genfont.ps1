# Generates oled_font.h / oled_font.c for the SSD1306 menu project.
# ASCII glyphs 8x16 from SimSun half-width forms, CJK glyphs 16x16 from SimSun.
# Output layout is SSD1306 page order (byte = 8 vertical pixels, LSB = top row).

Add-Type -AssemblyName System.Drawing

$OutDir = 'F:\Stm32\Project\User'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# ---------------------------------------------------------------- glyph source
# code point, macro name, ascii-only comment.  MUST stay sorted ascending:
# Font_FindCjk() binary-searches this table.
$Cjk = @(
    @(0x4E3B, 'CN_ZHU',    'zhu    = main / host'),
    @(0x4E8E, 'CN_YU',     'yu     = at / to'),
    @(0x4EA4, 'CN_JIAO',   'jiao   = alternate'),
    @(0x4EAE, 'CN_LIANG',  'liang  = bright'),
    @(0x4F20, 'CN_CHUAN',  'chuan  = transmit'),
    @(0x4FE1, 'CN_XIN',    'xin    = information'),
    @(0x504F, 'CN_PIAN2',  'pian   = deviate'),
    @(0x505C, 'CN_TING',   'ting   = stop'),
    @(0x5149, 'CN_GUANG',  'guang  = light'),
    @(0x5173, 'CN_GUAN',   'guan   = close / about'),
    @(0x5236, 'CN_ZHI',    'zhi    = make'),
    @(0x529F, 'CN_GONG',   'gong   = function'),
    @(0x52A8, 'CN_DONG',   'dong   = move'),
    @(0x5341, 'CN_SHI5',   'shi    = ten / cross'),
    @(0x5355, 'CN_DAN',    'dan    = list / sheet'),
    @(0x53D1, 'CN_FA',     'fa     = start off'),
    @(0x53F7, 'CN_HAO',    'hao    = number'),
    @(0x540D, 'CN_MING2',  'ming   = name'),
    @(0x542F, 'CN_QI',     'qi     = start'),
    @(0x5668, 'CN_QI2',    'qi     = device'),
    @(0x56DE, 'CN_HUI',    'hui    = back'),
    @(0x5706, 'CN_YUAN',   'yuan   = round'),
    @(0x5708, 'CN_QUAN',   'quan   = lap / ring'),
    @(0x590D, 'CN_FU',     'fu     = again'),
    @(0x59D3, 'CN_XING2',  'xing   = surname'),
    @(0x5B57, 'CN_ZI',     'zi     = character'),
    @(0x5B66, 'CN_XUE',    'xue    = study'),
    @(0x5B8C, 'CN_WAN2',   'wan    = finish'),
    @(0x5C31, 'CN_JIU',    'jiu    = ready'),
    @(0x5C55, 'CN_ZHAN',   'zhan   = spread'),
    @(0x5DE1, 'CN_XUN',    'xun    = patrol'),
    @(0x5DEE, 'CN_CHA',    'cha    = difference'),
    @(0x5EA6, 'CN_DU',     'du     = degree / level'),
    @(0x5F00, 'CN_KAI',    'kai    = open / on'),
    @(0x5F0F, 'CN_SHI2',   'shi    = style / type'),
    @(0x5F2F, 'CN_WAN',    'wan    = bend / turn'),
    @(0x6001, 'CN_TAI',    'tai    = condition'),
    @(0x6062, 'CN_HUI2',   'hui    = restore'),
    @(0x606F, 'CN_XI',     'xi     = breath / news'),
    @(0x611F, 'CN_GAN',    'gan    = sense'),
    @(0x6210, 'CN_CHENG',  'cheng  = complete'),
    @(0x6253, 'CN_DA',     'da     = hit / open'),
    @(0x627E, 'CN_ZHAO',   'zhao   = seek'),
    @(0x62D3, 'CN_TUO',    'tuo    = expand'),
    @(0x6309, 'CN_AN',     'an     = press'),
    @(0x63A7, 'CN_KONG',   'kong   = control'),
    @(0x65AD, 'CN_DUAN',   'duan   = broken'),
    @(0x65F6, 'CN_SHI3',   'shi    = time'),
    @(0x663E, 'CN_XIAN',   'xian   = show'),
    @(0x66FF, 'CN_TI',     'ti     = substitute'),
    @(0x672C, 'CN_BEN',    'ben    = this'),
    @(0x673A, 'CN_JI',     'ji     = machine'),
    @(0x6A21, 'CN_MO',     'mo     = model / mode'),
    @(0x6B62, 'CN_ZHI2',   'zhi    = halt'),
    @(0x6D4B, 'CN_CE',     'ce     = measure'),
    @(0x6F14, 'CN_YAN',    'yan    = perform / demo'),
    @(0x706D, 'CN_MIE',    'mie    = extinguish / off'),
    @(0x70C1, 'CN_SHUO',   'shuo   = sparkle'),
    @(0x7247, 'CN_PIAN',   'pian   = piece / chip'),
    @(0x7248, 'CN_BAN',    'ban    = version'),
    @(0x72B6, 'CN_ZHUANG', 'zhuang = state'),
    @(0x73AF, 'CN_HUAN',   'huan   = ring'),
    @(0x7528, 'CN_YONG',   'yong   = use'),
    @(0x793A, 'CN_SHI',    'shi    = show'),
    @(0x7CFB, 'CN_XI2',    'xi     = system'),
    @(0x7EBF, 'CN_XIAN2',  'xian   = line'),
    @(0x7EDF, 'CN_TONG',   'tong   = whole'),
    @(0x7EEA, 'CN_XU',     'xu     = order / ready'),
    @(0x7F6E, 'CN_ZHI3',   'zhi    = place / set'),
    @(0x80CC, 'CN_BEI',    'bei    = back'),
    @(0x80FD, 'CN_NENG',   'neng   = ability'),
    @(0x82AF, 'CN_XIN2',   'xin    = chip core'),
    @(0x83DC, 'CN_CAI',    'cai    = dish / menu'),
    @(0x8702, 'CN_FENG',   'feng   = bee / buzzer'),
    @(0x884C, 'CN_XING',   'xing   = walk / run'),
    @(0x8BA4, 'CN_REN',    'ren    = recognise'),
    @(0x8BBE, 'CN_SHE',    'she    = set up'),
    @(0x8BD5, 'CN_SHI4',   'shi    = try / test'),
    @(0x8F66, 'CN_CHE',    'che    = car'),
    @(0x8F6C, 'CN_ZHUAN',  'zhuan  = turn'),
    @(0x8FD0, 'CN_YUN',    'yun    = run'),
    @(0x8FD4, 'CN_FAN',    'fan    = return'),
    @(0x901F, 'CN_SU',     'su     = speed'),
    @(0x952E, 'CN_JIAN2',  'jian   = key'),
    @(0x95EA, 'CN_SHAN',   'shan   = flash'),
    @(0x95ED, 'CN_BI',     'bi     = close'),
    @(0x95F4, 'CN_JIAN',   'jian   = between'),
    @(0x9891, 'CN_PIN',    'pin    = frequency'),
    @(0x9E23, 'CN_MING',   'ming   = cry / beep'),
    @(0x9ED8, 'CN_MO2',    'mo     = silent / default')
)

for ($i = 1; $i -lt $Cjk.Count; $i++) {
    if ([int]$Cjk[$i][0] -le [int]$Cjk[$i - 1][0]) {
        throw ("CJK table not strictly ascending at index {0}: 0x{1:X4} after 0x{2:X4}" -f $i, [int]$Cjk[$i][0], [int]$Cjk[$i - 1][0])
    }
}

# ------------------------------------------------------------------ rendering
function New-GlyphBitmap([int]$w, [int]$h) {
    $b = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($b)
    $g.Clear([System.Drawing.Color]::Black)
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
    return @($b, $g)
}

function Get-GlyphPixels($bmp, [int]$w, [int]$h) {
    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $len = $data.Stride * $h
    $bytes = New-Object byte[] $len
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $len)
    $bmp.UnlockBits($data)
    $grid = New-Object 'int[,]' $h, $w
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $off = $y * $data.Stride + $x * 4
            if ($bytes[$off] -gt 100) { $grid[$y, $x] = 1 } else { $grid[$y, $x] = 0 }
        }
    }
    return @($grid, $data.Stride)
}

# grid -> SSD1306 page bytes: first w bytes = cols for rows 0..7, next w = rows 8..15
function ConvertTo-PageBytes($grid, [int]$w, [int]$h) {
    $out = New-Object byte[] ($w * 2)
    for ($p = 0; $p -lt 2; $p++) {
        for ($x = 0; $x -lt $w; $x++) {
            $v = 0
            for ($b = 0; $b -lt 8; $b++) {
                $y = $p * 8 + $b
                if ($y -lt $h -and $grid[$y, $x] -eq 1) { $v = $v -bor (1 -shl $b) }
            }
            $out[$p * $w + $x] = [byte]$v
        }
    }
    return $out
}

function Format-ByteArray($bytes, [int]$perLine) {
    $sb = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        if ($i % $perLine -eq 0) { [void]$sb.Append('    ') }
        [void]$sb.Append(('0x{0:X2},' -f $bytes[$i]))
        if ($i % $perLine -eq ($perLine - 1)) { [void]$sb.Append("`r`n") } else { [void]$sb.Append(' ') }
    }
    if ($bytes.Length % $perLine -ne 0) { [void]$sb.Append("`r`n") }
    return $sb.ToString()
}

# ------------------------------------------------------------------- CJK pass
$fontCjk = New-Object System.Drawing.Font 'SimSun', 16, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
$preview = New-Object System.Text.StringBuilder
$cjkBytes = @()
$cjkCodes = @()
$cjkWarn = @()

[void]$preview.AppendLine('=== CJK 16x16 preview ===')
foreach ($entry in $Cjk) {
    $cp = [int]$entry[0]
    $name = $entry[1]
    $ch = [string][char]$cp
    $pair = New-GlyphBitmap 16 16
    $bmp = $pair[0]; $g = $pair[1]
    $g.DrawString($ch, $fontCjk, [System.Drawing.Brushes]::White, 0, 0, [System.Drawing.StringFormat]::GenericTypographic)
    $g.Dispose()
    $pr = Get-GlyphPixels $bmp 16 16
    $grid = $pr[0]
    $bmp.Dispose()

    for ($y = 0; $y -lt 16; $y++) {
        $line = ''
        for ($x = 0; $x -lt 16; $x++) { if ($grid[$y, $x] -eq 1) { $line += '#' } else { $line += '.' } }
        [void]$preview.AppendLine($line)
    }
    [void]$preview.AppendLine("--- $name U+$('{0:X4}' -f $cp)")

    $blank = $true
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) { if ($grid[$y, $x] -eq 1) { $blank = $false } } }
    if ($blank) { $cjkWarn += $name }

    $cjkBytes += , (ConvertTo-PageBytes $grid 16 16)
    $cjkCodes += $cp
}
$fontCjk.Dispose()

# ----------------------------------------------------------------- ASCII pass
$fontAsc = New-Object System.Drawing.Font 'SimSun', 16, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
$ascBytes = @()
$ascWarn = @()
[void]$preview.AppendLine('=== ASCII 8x16 preview (0x20-0x7E) ===')
for ($c = 0x20; $c -le 0x7E; $c++) {
    $ch = [string][char]$c
    $pair = New-GlyphBitmap 8 16
    $bmp = $pair[0]; $g = $pair[1]
    if ($c -ne 0x20) {
        $g.DrawString($ch, $fontAsc, [System.Drawing.Brushes]::White, 0, 0, [System.Drawing.StringFormat]::GenericTypographic)
    }
    $g.Dispose()
    $pr = Get-GlyphPixels $bmp 8 16
    $grid = $pr[0]
    $bmp.Dispose()
    $ascBytes += , (ConvertTo-PageBytes $grid 8 16)

    for ($y = 0; $y -lt 16; $y++) {
        $line = ''
        for ($x = 0; $x -lt 8; $x++) { if ($grid[$y, $x] -eq 1) { $line += '#' } else { $line += '.' } }
        [void]$preview.AppendLine(('{0} |{1}|' -f $ch, $line))
    }
}
$fontAsc.Dispose()

# -------------------------------------------------------------------- emit C
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('/*')
[void]$sb.AppendLine(' * oled_font.h - font tables for the SSD1306 menu project')
[void]$sb.AppendLine(' *')
[void]$sb.AppendLine(' * AUTO-GENERATED by tools/genfont.ps1 - do not edit by hand.')
[void]$sb.AppendLine(' * ASCII : 8x16, SimSun half-width forms, index = ascii - 0x20  (95 glyphs)')
[void]$sb.AppendLine(' * CJK   : 16x16, SimSun, looked up by Unicode code point')
[void]$sb.AppendLine(' *')
[void]$sb.AppendLine(' * Storage order is native SSD1306 page order: byte k of a glyph is one')
[void]$sb.AppendLine(' * column, bit 0 = topmost row of that 8-row page.  A 16x16 glyph is')
[void]$sb.AppendLine(' * bytes[0..15] = page N (rows 0..7) then bytes[16..31] = page N+1.')
[void]$sb.AppendLine(' * Strings are arrays of Unicode code points, so the source files stay')
[void]$sb.AppendLine(' * pure ASCII and never depend on the editor/compiler text encoding.')
[void]$sb.AppendLine(' */')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#ifndef __OLED_FONT_H')
[void]$sb.AppendLine('#define __OLED_FONT_H')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#include <stdint.h>')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#define FONT_ASCII_FIRST  0x20u')
[void]$sb.AppendLine('#define FONT_ASCII_LAST   0x7Eu')
[void]$sb.AppendLine('#define FONT_ASCII_COUNT  95u')
[void]$sb.AppendLine('#define FONT_CJK_COUNT    ' + $Cjk.Count + 'u')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('/* Unicode code points of every CJK glyph in the table */')
foreach ($entry in $Cjk) {
    [void]$sb.AppendLine(('#define {0,-10} 0x{1:X4}u   /* {2} */' -f $entry[1], [int]$entry[0], $entry[2]))
}
[void]$sb.AppendLine('')
[void]$sb.AppendLine('extern const uint8_t font_ascii_8x16[FONT_ASCII_COUNT][16];')
[void]$sb.AppendLine('extern const uint16_t font_cjk_code[FONT_CJK_COUNT];')
[void]$sb.AppendLine('extern const uint8_t font_cjk_16x16[FONT_CJK_COUNT][32];')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('/* returns glyph index, or -1 when the character has no bitmap */')
[void]$sb.AppendLine('int Font_FindCjk(uint16_t code);')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#endif /* __OLED_FONT_H */')

Set-Content -Path (Join-Path $OutDir 'oled_font.h') -Value $sb.ToString() -Encoding ASCII

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('/* AUTO-GENERATED by tools/genfont.ps1 - do not edit by hand. */')
[void]$sb.AppendLine('#include "oled_font.h"')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('/* ASCII 8x16, index = character - 0x20 */')
[void]$sb.AppendLine('const uint8_t font_ascii_8x16[FONT_ASCII_COUNT][16] = {')
for ($i = 0; $i -lt 95; $i++) {
    $c = 0x20 + $i
    $label = if ($c -eq 0x20) { 'space' } elseif ($c -eq 0x27) { 'quote' } elseif ($c -eq 0x5C) { 'backslash' } else { [string][char]$c }
    $line = ''
    foreach ($b in $ascBytes[$i]) { $line += ('0x{0:X2},' -f $b) }
    [void]$sb.AppendLine(('  { ' + $line + ' },   /* ' + $label + ' */'))
}
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('/* Unicode code points, kept sorted for the binary search below */')
[void]$sb.AppendLine('const uint16_t font_cjk_code[FONT_CJK_COUNT] = {')
$line = '   '
for ($i = 0; $i -lt $Cjk.Count; $i++) {
    $line += (' 0x{0:X4},' -f [int]$Cjk[$i][0])
    if (($i + 1) % 8 -eq 0) { [void]$sb.AppendLine($line); $line = '   ' }
}
if ($line.Trim().Length -gt 0) { [void]$sb.AppendLine($line) }
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('/* CJK 16x16, same order as font_cjk_code[] */')
[void]$sb.AppendLine('const uint8_t font_cjk_16x16[FONT_CJK_COUNT][32] = {')
for ($i = 0; $i -lt $Cjk.Count; $i++) {
    $top = ''
    $bot = ''
    for ($k = 0; $k -lt 16; $k++) {
        $top += ('0x{0:X2},' -f $cjkBytes[$i][$k])
        $bot += ('0x{0:X2},' -f $cjkBytes[$i][16 + $k])
        if ($k -lt 15) { $top += ' '; $bot += ' ' }
    }
    $tail = ('   /* U+{0:X4} {1} */' -f ([int]$Cjk[$i][0]), $Cjk[$i][2])
    [void]$sb.AppendLine(('  { ' + $top) + $tail)
    [void]$sb.AppendLine(('    ' + $bot) + ' },')
}
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('int Font_FindCjk(uint16_t code)')
[void]$sb.AppendLine('{')
[void]$sb.AppendLine('    int lo = 0;')
[void]$sb.AppendLine('    int hi = (int)FONT_CJK_COUNT - 1;')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('    while (lo <= hi) {')
[void]$sb.AppendLine('        int mid = (lo + hi) >> 1;')
[void]$sb.AppendLine('        if (font_cjk_code[mid] == code) {')
[void]$sb.AppendLine('            return mid;')
[void]$sb.AppendLine('        } else if (font_cjk_code[mid] < code) {')
[void]$sb.AppendLine('            lo = mid + 1;')
[void]$sb.AppendLine('        } else {')
[void]$sb.AppendLine('            hi = mid - 1;')
[void]$sb.AppendLine('        }')
[void]$sb.AppendLine('    }')
[void]$sb.AppendLine('    return -1;')
[void]$sb.AppendLine('}')

Set-Content -Path (Join-Path $OutDir 'oled_font.c') -Value $sb.ToString() -Encoding ASCII

Set-Content -Path 'F:\Stm32\Project\tools\font-preview.txt' -Value $preview.ToString() -Encoding ASCII

Write-Output ("CJK glyphs: {0}  ASCII glyphs: {1}" -f $Cjk.Count, $ascBytes.Count)
if ($cjkWarn.Count -gt 0) { Write-Output ("BLANK CJK GLYPHS: " + ($cjkWarn -join ', ')) } else { Write-Output 'no blank CJK glyphs' }
Write-Output 'done'
