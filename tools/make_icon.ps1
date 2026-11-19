# 生成 res/app.ico（多尺寸 PNG 压缩 ICO）
# 用法：powershell -ExecutionPolicy Bypass -File tools/make_icon.ps1
Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$resDir = Join-Path $root 'res'
$tempDir = Join-Path $env:TEMP 'regfileviewer_icon'
New-Item -ItemType Directory -Force $resDir, $tempDir | Out-Null

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$entries = @()

foreach ($size in $sizes) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $radius = [single]($size * 0.20)
    $diameter = [single]($radius * 2)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc(0, 0, $diameter, $diameter, 180, 90)
    $path.AddArc($size - $diameter, 0, $diameter, $diameter, 270, 90)
    $path.AddArc($size - $diameter, $size - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc(0, $size - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()

    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(31, 111, 235))
    $graphics.FillPath($brush, $path)

    $fontSize = [single]($size * 0.42)
    $font = New-Object System.Drawing.Font('Segoe UI', $fontSize,
        [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
    $rect = New-Object System.Drawing.RectangleF 0, 0, $size, $size
    $graphics.DrawString('RF', $font, $white, $rect, $format)

    $pngPath = Join-Path $tempDir ("icon_{0}.png" -f $size)
    $bitmap.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)

    $graphics.Dispose()
    $bitmap.Dispose()

    $entries += [PSCustomObject]@{
        Size  = $size
        Bytes = [IO.File]::ReadAllBytes($pngPath)
    }
}

$stream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($stream)

# ICONDIR
$writer.Write([UInt16]0)
$writer.Write([UInt16]1)
$writer.Write([UInt16]$entries.Count)

# ICONDIRENTRY
$offset = 6 + 16 * $entries.Count
foreach ($entry in $entries) {
    $dimension = if ($entry.Size -ge 256) { 0 } else { $entry.Size }
    $writer.Write([Byte]$dimension)
    $writer.Write([Byte]$dimension)
    $writer.Write([Byte]0)
    $writer.Write([Byte]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]$entry.Bytes.Length)
    $writer.Write([UInt32]$offset)
    $offset += $entry.Bytes.Length
}

# PNG 数据
foreach ($entry in $entries) {
    $writer.Write($entry.Bytes)
}

$writer.Flush()
$icoPath = Join-Path $resDir 'app.ico'
[IO.File]::WriteAllBytes($icoPath, $stream.ToArray())

$writer.Dispose()
$stream.Dispose()
Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "生成 $icoPath ($($entries.Count) 个尺寸)"
