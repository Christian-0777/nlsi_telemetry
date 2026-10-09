param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath64,

    [Parameter(Mandatory = $true)]
    [string]$DllPath32,

    [Parameter(Mandatory = $true)]
    [string]$ManifestPath,

    [Parameter(Mandatory = $true)]
    [string]$StatusPath,

    [switch]$RemoveManagedPlugin
)

$ErrorActionPreference = 'Stop'
$expectedHashes = @{
    'win_x64' = '4F1A1DD5B879773161C23D657249775D60C9AA362CED171D74C74A16F1AB0F0A'
    'win_x86' = '01E5D1CD6AF7C239A7B9E80E9911DE447FC8E80F66EE8F415D859A89F4A403BF'
}
$sources = @{
    'win_x64' = (Resolve-Path -LiteralPath $DllPath64 -ErrorAction Stop).Path
    'win_x86' = (Resolve-Path -LiteralPath $DllPath32 -ErrorAction Stop).Path
}
$statusDirectory = Split-Path -Parent $StatusPath
$manifestDirectory = Split-Path -Parent $ManifestPath
New-Item -ItemType Directory -Path $statusDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $manifestDirectory -Force | Out-Null

function Write-Status([string]$Message) {
    $line = '{0:u} {1}' -f (Get-Date), $Message
    Add-Content -LiteralPath $StatusPath -Value $line -Encoding UTF8
}

function Get-SteamLibraries {
    $roots = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($candidate in @(
        'C:\Program Files (x86)\Steam',
        'C:\Program Files\Steam'
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            [void]$roots.Add([System.IO.Path]::GetFullPath($candidate))
        }
    }

    foreach ($registryPath in @(
        'HKLM:\SOFTWARE\Valve\Steam',
        'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam',
        'HKCU:\SOFTWARE\Valve\Steam'
    )) {
        if (Test-Path -LiteralPath $registryPath) {
            $properties = Get-ItemProperty -LiteralPath $registryPath -ErrorAction SilentlyContinue
            foreach ($propertyName in @('InstallPath', 'SteamPath')) {
                $candidate = $properties.$propertyName
                if (-not [string]::IsNullOrWhiteSpace($candidate) -and
                    (Test-Path -LiteralPath $candidate -PathType Container)) {
                    [void]$roots.Add([System.IO.Path]::GetFullPath($candidate))
                }
            }
        }
    }

    foreach ($root in @($roots)) {
        $libraryFolders = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $libraryFolders -PathType Leaf)) {
            continue
        }
        $content = Get-Content -LiteralPath $libraryFolders -Raw
        foreach ($match in [regex]::Matches($content, '"path"\s+"([^"]+)"')) {
            $library = $match.Groups[1].Value.Replace('\\', '\')
            if (Test-Path -LiteralPath $library -PathType Container) {
                [void]$roots.Add([System.IO.Path]::GetFullPath($library))
            }
        }
    }
    return @($roots)
}

$gameFolders = @(
    'Euro Truck Simulator 2',
    'American Truck Simulator'
)
$pluginName = 'trucksim-gps-telemetry.dll'
$knownDestinations = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
$libraries = @(Get-SteamLibraries)
foreach ($library in $libraries) {
    foreach ($gameFolder in $gameFolders) {
        foreach ($architecture in @('win_x64', 'win_x86')) {
            $pluginDirectory = Join-Path $library "steamapps\common\$gameFolder\bin\$architecture\plugins"
            [void]$knownDestinations.Add([System.IO.Path]::GetFullPath(
                (Join-Path $pluginDirectory $pluginName)))
        }
    }
}

foreach ($architecture in @('win_x64', 'win_x86')) {
    $actualHash = (Get-FileHash -LiteralPath $sources[$architecture] -Algorithm SHA256).Hash
    if ($actualHash -ine $expectedHashes[$architecture]) {
        throw "TruckSim GPS $architecture plugin hash mismatch: expected $($expectedHashes[$architecture]), got $actualHash."
    }
}

if ($RemoveManagedPlugin) {
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        Write-Status 'Uninstall: no managed TruckSim GPS plugin copies were recorded.'
        exit 0
    }
    $managed = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    foreach ($entry in $managed.PSObject.Properties) {
        $destination = [System.IO.Path]::GetFullPath($entry.Name)
        if (-not $knownDestinations.Contains($destination)) {
            Write-Status "Uninstall: ignored unrecognized game plugin path: $destination"
            continue
        }
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            Write-Status "Uninstall: already absent: $destination"
            continue
        }
        $actualHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($actualHash -ine [string]$entry.Value) {
            Write-Status "Uninstall: preserved changed or different plugin: $destination"
            continue
        }
        try {
            Remove-Item -LiteralPath $destination -Force
            Write-Status "Uninstall: removed managed plugin: $destination"
        }
        catch {
            Write-Status "Uninstall: could not remove $destination; close the game and remove it manually if desired. $($_.Exception.Message)"
        }
    }
    exit 0
}

$previousHashes = @{}
if (Test-Path -LiteralPath $ManifestPath -PathType Leaf) {
    try {
        $previous = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
        foreach ($entry in $previous.PSObject.Properties) {
            if (-not [string]::IsNullOrWhiteSpace($entry.Name)) {
                $previousHashes[[System.IO.Path]::GetFullPath($entry.Name)] = [string]$entry.Value
            }
        }
    }
    catch {
        throw "Could not read the existing TruckSim GPS plugin manifest; no existing DLL will be overwritten. $($_.Exception.Message)"
    }
}

$managedHashes = $previousHashes.Clone()
$failures = [System.Collections.Generic.List[string]]::new()
$installed = 0
foreach ($library in $libraries) {
    foreach ($gameFolder in $gameFolders) {
        $gameRoot = Join-Path $library "steamapps\common\$gameFolder"
        if (-not (Test-Path -LiteralPath $gameRoot -PathType Container)) {
            continue
        }
        foreach ($architecture in @('win_x64', 'win_x86')) {
            $architectureRoot = Join-Path $gameRoot "bin\$architecture"
            if (-not (Test-Path -LiteralPath $architectureRoot -PathType Container)) {
                Write-Status "Install: skipped $architecture because the game has no matching architecture directory: $gameRoot"
                continue
            }
            $pluginDirectory = Join-Path $gameRoot "bin\$architecture\plugins"
            $destination = [System.IO.Path]::GetFullPath(
                (Join-Path $pluginDirectory $pluginName))
            try {
                New-Item -ItemType Directory -Path $pluginDirectory -Force | Out-Null
                $sourceHash = $expectedHashes[$architecture]
                $previousHash = $previousHashes[$destination]
                if (Test-Path -LiteralPath $destination -PathType Leaf) {
                    $currentHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
                    if ($currentHash -ieq $sourceHash) {
                        if ($previousHash -ieq $currentHash) {
                            $managedHashes[$destination] = $currentHash
                            Write-Status "Install: already managed TruckSim GPS plugin is current: $destination"
                        }
                        else {
                            Write-Status "Install: preserved an identical plugin not managed by this installer: $destination"
                        }
                        continue
                    }
                    if ([string]::IsNullOrWhiteSpace($previousHash) -or
                        $currentHash -ine $previousHash) {
                        Write-Status "Install: preserved a different or modified plugin instead of overwriting it: $destination"
                        continue
                    }
                }

                $temporary = "$destination.nlsi-$([guid]::NewGuid().ToString('N')).tmp"
                $backup = "$destination.nlsi-$([guid]::NewGuid().ToString('N')).bak"
                try {
                    [System.IO.File]::Copy($sources[$architecture], $temporary, $false)
                    if (Test-Path -LiteralPath $destination -PathType Leaf) {
                        [System.IO.File]::Replace($temporary, $destination, $backup)
                        Write-Status "Install: previous managed plugin preserved for recovery at $backup"
                    }
                    else {
                        [System.IO.File]::Move($temporary, $destination)
                    }
                }
                finally {
                    if (Test-Path -LiteralPath $temporary -PathType Leaf) {
                        Remove-Item -LiteralPath $temporary -Force
                    }
                }
                $installedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
                if ($installedHash -ine $sourceHash) {
                    throw "Installed plugin hash did not match the verified source: $destination"
                }
                $managedHashes[$destination] = $installedHash
                $installed++
                Write-Status "Install: installed $architecture TruckSim GPS plugin: $destination"
            }
            catch {
                $message = "Install: failed to install TruckSim GPS plugin at $destination. $($_.Exception.Message)"
                $failures.Add($message)
                Write-Status $message
            }
        }
    }
}

$managedHashes | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $ManifestPath -Encoding UTF8
if ($libraries.Count -eq 0 -or
    ($installed -eq 0 -and $failures.Count -eq 0 -and $managedHashes.Count -eq 0)) {
    Write-Status 'Install: no supported Steam ETS2/ATS plugin destination was found.'
}
if ($failures.Count -gt 0) {
    throw "TruckSim GPS plugin installation failed for $($failures.Count) destination(s); see $StatusPath."
}
if ($installed -eq 0 -and $managedHashes.Count -gt 0) {
    Write-Status 'Install: managed TruckSim GPS plugins are already current.'
}
