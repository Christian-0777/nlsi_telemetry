param(
    [Parameter(Mandatory = $true)]
    [string]$BackgroundPath,
    [Parameter(Mandatory = $true)]
    [string]$LogoPath,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$background = [System.Drawing.Image]::FromFile($BackgroundPath)
$backgroundBitmap = New-Object System.Drawing.Bitmap(164, 314, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$graphics = [System.Drawing.Graphics]::FromImage($backgroundBitmap)
$graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$targetAspect = 164.0 / 314.0
$sourceAspect = $background.Width / [double]$background.Height
if ($sourceAspect -gt $targetAspect) {
    $sourceWidth = [int][Math]::Round($background.Height * $targetAspect)
    $sourceX = [int](($background.Width - $sourceWidth) / 2)
    $sourceRectangle = New-Object System.Drawing.Rectangle($sourceX, 0, $sourceWidth, $background.Height)
}
else {
    $sourceHeight = [int][Math]::Round($background.Width / $targetAspect)
    $sourceY = [int](($background.Height - $sourceHeight) / 2)
    $sourceRectangle = New-Object System.Drawing.Rectangle(0, $sourceY, $background.Width, $sourceHeight)
}
$graphics.DrawImage(
    $background,
    (New-Object System.Drawing.Rectangle(0, 0, 164, 314)),
    $sourceRectangle,
    [System.Drawing.GraphicsUnit]::Pixel
)
$backgroundBitmap.Save((Join-Path $OutputDirectory 'wizard-background.bmp'), [System.Drawing.Imaging.ImageFormat]::Bmp)
$graphics.Dispose()
$backgroundBitmap.Dispose()
$background.Dispose()

$logo = [System.Drawing.Image]::FromFile($LogoPath)
$logoBitmap = New-Object System.Drawing.Bitmap(55, 55, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$logoGraphics = [System.Drawing.Graphics]::FromImage($logoBitmap)
$logoGraphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$logoGraphics.Clear([System.Drawing.Color]::FromArgb(255, 255, 247, 250))
$logoGraphics.DrawImage($logo, (New-Object System.Drawing.Rectangle(0, 0, 55, 55)))
$logoBitmap.Save((Join-Path $OutputDirectory 'wizard-logo.bmp'), [System.Drawing.Imaging.ImageFormat]::Bmp)
$logoGraphics.Dispose()
$logoBitmap.Dispose()
$logo.Dispose()
