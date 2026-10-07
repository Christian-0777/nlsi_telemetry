param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath,

    [Parameter(Mandatory = $true)]
    [string]$ManifestPath,

    [Parameter(Mandatory = $true)]
    [string]$StatusPath,

    [switch]$RemoveInstalledPlugins
)

$ErrorActionPreference = 'Stop'

function Write-InstallerLog([string]$Message) {
    $line = '{0:u} {1}' -f (Get-Date), $Message
    $line | Add-Content -LiteralPath $logPath -Encoding UTF8
}

function Add-LibraryRoot(
    [System.Collections.Generic.HashSet[string]]$LibraryRoots,
    [string]$Candidate
) {
    if ([string]::IsNullOrWhiteSpace($Candidate)) {
        return
    }

    $normalized = [System.IO.Path]::GetFullPath($Candidate.Trim().Replace('/', '\'))
    if ($normalized.EndsWith('\steamapps', [System.StringComparison]::OrdinalIgnoreCase)) {
        $normalized = [System.IO.Path]::GetDirectoryName($normalized)
    }
    [void]$LibraryRoots.Add($normalized)
}

function Get-SteamLibraries {
    $libraryPathSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($root in @(
        'C:\Program Files (x86)\Steam',
        'C:\Program Files\Steam',
        'D:\Steam',
        'D:\SteamLibrary',
        'E:\SteamLibrary'
    )) {
        if (Test-Path -LiteralPath $root -PathType Container) {
            [void]$libraryPathSet.Add([System.IO.Path]::GetFullPath($root))
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
                $registeredPath = $properties.$propertyName
                if (-not [string]::IsNullOrWhiteSpace($registeredPath)) {
                    Add-LibraryRoot $libraryPathSet $registeredPath
                }
            }
        }
    }

    foreach ($drive in (Get-PSDrive -PSProvider FileSystem)) {
        foreach ($candidate in @(
            (Join-Path $drive.Root 'Steam'),
            (Join-Path $drive.Root 'SteamLibrary')
        )) {
            if (Test-Path -LiteralPath $candidate -PathType Container) {
                [void]$libraryPathSet.Add([System.IO.Path]::GetFullPath($candidate))
            }
        }
    }

    $configuredRoots = @($libraryPathSet | ForEach-Object { $_ })
    foreach ($root in $configuredRoots) {
        $libraryConfig = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $libraryConfig -PathType Leaf)) {
            continue
        }

        $content = Get-Content -LiteralPath $libraryConfig -Raw
        foreach ($match in [regex]::Matches($content, '"path"\s+"([^"]+)"')) {
            $vdfPath = $match.Groups[1].Value.Replace('\\', '\')
            Add-LibraryRoot $libraryPathSet $vdfPath
        }
    }

    return @($libraryPathSet | ForEach-Object { $_ })
}

$resolvedDllPath = (Resolve-Path -LiteralPath $DllPath -ErrorAction Stop).Path
$logPath = Join-Path (Split-Path -Parent $StatusPath) 'game-plugin-install.log'
$statusDirectory = Split-Path -Parent $StatusPath
New-Item -ItemType Directory -Path $statusDirectory -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $ManifestPath) -Force | Out-Null

if ($RemoveInstalledPlugins) {
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        Write-InstallerLog 'Uninstall: no managed game plugin copies were recorded.'
        exit 0
    }

    $knownPluginPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($library in (Get-SteamLibraries)) {
        foreach ($gameDirectory in @('Euro Truck Simulator 2', 'American Truck Simulator')) {
            $knownPath = Join-Path $library "steamapps\common\$gameDirectory\bin\win_x64\plugins\nlsi_telemetry.dll"
            [void]$knownPluginPaths.Add([System.IO.Path]::GetFullPath($knownPath))
        }
    }

    $expectedHash = (Get-FileHash -LiteralPath $resolvedDllPath -Algorithm SHA256).Hash
    foreach ($destination in (Get-Content -LiteralPath $ManifestPath)) {
        if ([string]::IsNullOrWhiteSpace($destination) -or
            [System.IO.Path]::GetFileName($destination) -ine 'nlsi_telemetry.dll') {
            continue
        }

        $normalizedDestination = [System.IO.Path]::GetFullPath($destination)
        if (-not $knownPluginPaths.Contains($normalizedDestination)) {
            Write-InstallerLog "Uninstall: ignored unrecognized plugin path: $destination"
            continue
        }

        if (-not (Test-Path -LiteralPath $normalizedDestination -PathType Leaf)) {
            Write-InstallerLog "Uninstall: already absent: $normalizedDestination"
            continue
        }

        try {
            $actualHash = (Get-FileHash -LiteralPath $normalizedDestination -Algorithm SHA256).Hash
            if ($actualHash -ine $expectedHash) {
                Write-InstallerLog "Uninstall: kept changed or different DLL: $normalizedDestination"
                continue
            }

            Remove-Item -LiteralPath $normalizedDestination -Force
            Write-InstallerLog "Uninstall: removed managed DLL: $normalizedDestination"
        }
        catch {
            Write-InstallerLog "Uninstall: could not remove $normalizedDestination. Close the game and remove this NLSI DLL manually if desired. $($_.Exception.Message)"
        }
    }

    exit 0
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('ETS2: NOT DETECTED')
$lines.Add('ATS: NOT DETECTED')
$failures = [System.Collections.Generic.List[string]]::new()
$managedDestinations = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

if (Test-Path -LiteralPath $ManifestPath -PathType Leaf) {
    foreach ($previousDestination in (Get-Content -LiteralPath $ManifestPath)) {
        if (-not [string]::IsNullOrWhiteSpace($previousDestination)) {
            [void]$managedDestinations.Add($previousDestination)
        }
    }
}

$games = @(
    @{
        Name = 'ETS2'
        Directory = 'Euro Truck Simulator 2'
    },
    @{
        Name = 'ATS'
        Directory = 'American Truck Simulator'
    }
)
$libraries = @(Get-SteamLibraries)

foreach ($game in $games) {
    $installed = $false
    $installationFailed = $false
    foreach ($library in $libraries) {
        $gameDirectory = Join-Path $library "steamapps\common\$($game.Directory)"
        if (-not (Test-Path -LiteralPath $gameDirectory -PathType Container)) {
            continue
        }

        $pluginDirectory = Join-Path $gameDirectory 'bin\win_x64\plugins'
        $destination = Join-Path $pluginDirectory 'nlsi_telemetry.dll'
        $temporarySuffix = [guid]::NewGuid().ToString('N')
        $temporaryDestination = "$destination.nlsi-$temporarySuffix.tmp"
        $backupDestination = "$destination.nlsi-$temporarySuffix.bak"
        try {
            New-Item -ItemType Directory -Path $pluginDirectory -Force | Out-Null
            [System.IO.File]::Copy($resolvedDllPath, $temporaryDestination, $false)
            if (Test-Path -LiteralPath $destination -PathType Leaf) {
                [System.IO.File]::Replace($temporaryDestination, $destination, $backupDestination)
                Remove-Item -LiteralPath $backupDestination -Force -ErrorAction SilentlyContinue
            }
            else {
                [System.IO.File]::Move($temporaryDestination, $destination)
            }

            [void]$managedDestinations.Add($destination)
            $statusIndex = if ($game.Name -eq 'ETS2') { 0 } else { 1 }
            $lines[$statusIndex] = "$($game.Name): INSTALLED - $pluginDirectory"
            Write-InstallerLog "Installed $($game.Name) plugin: $destination"
            $installed = $true
            break
        }
        catch {
            $failure = "$($game.Name) plugin installation failed at $destination. Close the game and rerun the installer. $($_.Exception.Message)"
            $failures.Add($failure)
            Write-InstallerLog $failure
            $installationFailed = $true
            $statusIndex = if ($game.Name -eq 'ETS2') { 0 } else { 1 }
            $lines[$statusIndex] = "$($game.Name): FAILED - Close the game and rerun setup; see the installer log."
            foreach ($temporaryFile in @($temporaryDestination, $backupDestination)) {
                if (Test-Path -LiteralPath $temporaryFile -PathType Leaf) {
                    Remove-Item -LiteralPath $temporaryFile -Force -ErrorAction SilentlyContinue
                }
            }
            break
        }
    }

    if (-not $installed -and -not $installationFailed) {
        Write-InstallerLog "$($game.Name): not detected in Steam libraries; skipped."
    }
}

$lines | Set-Content -LiteralPath $StatusPath -Encoding UTF8
$managedDestinations | Set-Content -LiteralPath $ManifestPath -Encoding UTF8

if ($failures.Count -gt 0) {
    throw ($failures -join [Environment]::NewLine)
}

foreach ($line in $lines) {
    Write-InstallerLog $line
}
