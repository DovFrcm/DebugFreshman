Add-Type -AssemblyName System.Drawing

$lines = Get-Content 'F:\Stm32\tests\test-output.txt'
$scale = 5
$outDir = 'F:\Stm32\tests\screens'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

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
