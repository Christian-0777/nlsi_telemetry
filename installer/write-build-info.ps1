param(
    [string]$Version,
    [string]$OutputPath,
    [string]$SdkRoot = ""
)

$root = Split-Path -Parent $PSScriptRoot
$pythonCmd = Get-Command python -ErrorAction SilentlyContinue
if (-not $pythonCmd) {
    $pythonCmd = Get-Command py -ErrorAction SilentlyContinue
}
if (-not $pythonCmd) {
    throw "Python was not found on PATH. Install Python 3 and try again."
}

if (-not $Version) {
    $Version = & $pythonCmd.Source (Join-Path $root "installer\read_version.py")
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to read the project version from version.json."
    }
    $Version = $Version.Trim()
}

if (-not $OutputPath) {
    $OutputPath = Join-Path $root ("build\v{0}\build-info.json" -f $Version)
}

& $pythonCmd.Source (Join-Path $root "installer\write_build_info.py") --version $Version --output $OutputPath --sdk-root $SdkRoot
exit $LASTEXITCODE
