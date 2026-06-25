param(
  [string]$Dist = "build-qt/dist"
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$distPath = (Resolve-Path -LiteralPath (Join-Path $root $Dist)).Path

function Fail([string]$Message) {
  throw $Message
}

function Test-AnyFile([string[]]$RelativePaths) {
  foreach ($relativePath in $RelativePaths) {
    if (Test-Path -LiteralPath (Join-Path $distPath $relativePath) -PathType Leaf) {
      return $true
    }
  }
  return $false
}

if (-not (Test-Path -LiteralPath (Join-Path $distPath "OpenListSorter.exe") -PathType Leaf)) {
  Fail "OpenListSorter.exe is missing from $distPath"
}

$cacheFiles = @()
$cacheDir = Join-Path $distPath "cache"
if (Test-Path -LiteralPath $cacheDir -PathType Container) {
  $cacheFiles = Get-ChildItem -LiteralPath $cacheDir -Recurse -File
}
if ($cacheFiles) {
  Fail ("Runtime cache files should not be packaged; found " +
    $cacheFiles.Count + " files under cache/")
}

$forbiddenFiles = @(
  "ffmpeg.exe",
  "ffplay.exe",
  "ffprobe.exe",
  "magick.exe",
  "runtime/imagemagick/CORE_RL_MagickWand_.dll",
  "runtime/imagemagick/CORE_RL_MagickCore_.dll",
  "runtime/imagecodecs/CORE_RL_MagickWand_.dll",
  "runtime/imagecodecs/CORE_RL_MagickCore_.dll",
  "imageformats/qgif.dll",
  "imageformats/qicns.dll",
  "imageformats/qico.dll",
  "imageformats/qjp2.dll",
  "imageformats/qjpeg.dll",
  "imageformats/qmng.dll",
  "imageformats/qtga.dll",
  "imageformats/qtiff.dll",
  "imageformats/qwbmp.dll",
  "imageformats/qwebp.dll"
)

foreach ($relativePath in $forbiddenFiles) {
  if (Test-Path -LiteralPath (Join-Path $distPath $relativePath) -PathType Leaf) {
    Fail "Forbidden runtime file is present: $relativePath"
  }
}

$debugDlls = Get-ChildItem -LiteralPath $distPath -Recurse -File -Filter "*d.dll" |
  Where-Object { $_.Name -ne "msvcp140d.dll" }
if ($debugDlls) {
  Fail ("Debug runtime DLLs are present: " +
    (($debugDlls | ForEach-Object { $_.FullName.Substring($distPath.Length + 1) }) -join ", "))
}

$imageFormatDlls = @()
$imageFormatDir = Join-Path $distPath "imageformats"
if (Test-Path -LiteralPath $imageFormatDir -PathType Container) {
  $imageFormatDlls = Get-ChildItem -LiteralPath $imageFormatDir -File -Filter "*.dll"
}
$nonSvgImageFormatDlls = $imageFormatDlls | Where-Object { $_.Name -ne "qsvg.dll" }
if ($nonSvgImageFormatDlls) {
  Fail ("Only qsvg.dll may remain in imageformats; found: " +
    (($nonSvgImageFormatDlls | ForEach-Object Name) -join ", "))
}

$imageCodecDir = Join-Path $distPath "runtime/imagecodecs"
if (Test-Path -LiteralPath $imageCodecDir -PathType Container) {
  $bundledMsvcCodecRuntime = Get-ChildItem -LiteralPath $imageCodecDir -File |
    Where-Object {
      $_.Name -like "msvcp140*.dll" -or
      $_.Name -like "vcruntime140*.dll" -or
      $_.Name -eq "vcomp140.dll"
    }
  if ($bundledMsvcCodecRuntime) {
    Fail ("VC++ codec runtime DLLs should not be bundled; found: " +
      (($bundledMsvcCodecRuntime | ForEach-Object Name) -join ", "))
  }
}

if (-not (Test-AnyFile @(
      "runtime/imagecodecs/libheif.dll",
      "runtime/imagecodecs/libheif-1.dll",
      "runtime/imagecodecs/CORE_RL_heif_.dll"
    ))) {
  Fail "No libheif runtime was found under runtime/imagecodecs"
}

if (-not (Test-AnyFile @(
      "runtime/imagecodecs/libwebp.dll",
      "runtime/imagecodecs/libwebp-7.dll",
      "runtime/imagecodecs/CORE_RL_webp_.dll"
    ))) {
  Fail "No libwebp runtime was found under runtime/imagecodecs"
}

$totalBytes = (Get-ChildItem -LiteralPath $distPath -Recurse -File |
  Measure-Object -Property Length -Sum).Sum
$totalMiB = [math]::Round($totalBytes / 1MB, 2)

Write-Host "Windows runtime check passed."
Write-Host "Dist: $distPath"
Write-Host "Image format plugins: $(($imageFormatDlls | ForEach-Object Name) -join ', ')"
Write-Host "Size: $totalMiB MiB"
