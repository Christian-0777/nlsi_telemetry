param(
    [string]$DllPath64,
    [string]$DllPath32,
    [Parameter(Mandatory = $true)]
    [string]$ManifestPath,
    [Parameter(Mandatory = $true)]
    [string]$BackupDirectory,
    [Parameter(Mandatory = $true)]
    [string]$StatusPath,
    [string[]]$SteamLibraryRoots,
    [switch]$RestoreManagedPlugin
)

$ErrorActionPreference = 'Stop'
$statusDirectory = Split-Path -Parent $StatusPath
$manifestDirectory = Split-Path -Parent $ManifestPath
New-Item -ItemType Directory -Path $statusDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $manifestDirectory -Force | Out-Null

function Write-Status([string]$Message) {
    Add-Content -LiteralPath $StatusPath -Value ('{0:u} {1}' -f (Get-Date), $Message) -Encoding UTF8
}

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Test-ManagedDestination([string]$Path) {
    $fullPath = Get-FullPath $Path
    return $fullPath -match '(?i)\\steamapps\\common\\(Euro Truck Simulator 2|American Truck Simulator)\\bin\\win_(x64|x86)\\plugins\\nlsi\.dll$'
}

function Get-SteamLibraries {
    $roots = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($candidate in @(
        (Join-Path ${env:ProgramFiles(x86)} 'Steam'),
        (Join-Path $env:ProgramFiles 'Steam')
    )) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Container)) {
            [void]$roots.Add((Get-FullPath $candidate))
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
                if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Container)) {
                    [void]$roots.Add((Get-FullPath $candidate))
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
                [void]$roots.Add((Get-FullPath $library))
            }
        }
    }
    return @($roots)
}

function Get-ManagedEntries {
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        return @()
    }
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    if ($manifest.version -ne 1) {
        throw 'The SCS position plugin manifest has an unsupported version.'
    }
    return @($manifest.plugins)
}

function Save-ManagedEntries($Entries) {
    $manifest = [ordered]@{
        version = 1
        plugins = @($Entries)
    }
    $temporary = "$ManifestPath.nlsi-$([guid]::NewGuid().ToString('N')).tmp"
    try {
        $manifest | ConvertTo-Json -Depth 5 |
            Set-Content -LiteralPath $temporary -Encoding UTF8
        Move-Item -LiteralPath $temporary -Destination $ManifestPath -Force
    }
    finally {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function Test-BackupPath($Entry) {
    if ([string]::IsNullOrWhiteSpace([string]$Entry.backup_path)) {
        return $false
    }
    $fullPath = Get-FullPath ([string]$Entry.backup_path)
    $root = (Get-FullPath $BackupDirectory).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to use a backup outside the installer-managed directory: $fullPath"
    }
    return (Test-Path -LiteralPath $fullPath -PathType Leaf)
}

if ($RestoreManagedPlugin) {
    foreach ($entry in (Get-ManagedEntries)) {
        $destination = Get-FullPath ([string]$entry.destination)
        if (-not (Test-ManagedDestination $destination)) {
            Write-Status "Uninstall: ignored an unrecognized managed destination: $destination"
            continue
        }
        $isInstalledCopy = $false
        if (Test-Path -LiteralPath $destination -PathType Leaf) {
            $currentHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
            $isInstalledCopy = $currentHash -ieq [string]$entry.installed_sha256
            if (-not $isInstalledCopy) {
                Write-Status "Uninstall: preserved a modified or different plugin: $destination"
                continue
            }
            Remove-Item -LiteralPath $destination -Force
            Write-Status "Uninstall: removed the unchanged managed plugin: $destination"
        }

        if (Test-BackupPath $entry) {
            $backupPath = Get-FullPath ([string]$entry.backup_path)
            $backupHash = (Get-FileHash -LiteralPath $backupPath -Algorithm SHA256).Hash
            if ($backupHash -ine [string]$entry.backup_sha256) {
                Write-Status "Uninstall: preserved a backup with an unexpected hash: $backupPath"
                continue
            }
            if (-not (Test-Path -LiteralPath (Split-Path -Parent $destination) -PathType Container)) {
                Write-Status "Uninstall: game directory is absent; did not recreate it: $destination"
                continue
            }
            if (Test-Path -LiteralPath $destination) {
                Write-Status "Uninstall: destination was recreated; did not overwrite it: $destination"
                continue
            }
            [System.IO.File]::Copy($backupPath, $destination, $false)
            Write-Status "Uninstall: restored the previous nlsi.dll: $destination"
        }
    }
    if (Test-Path -LiteralPath $ManifestPath -PathType Leaf) {
        Remove-Item -LiteralPath $ManifestPath -Force
    }
    exit 0
}

if (-not $DllPath64 -or -not $DllPath32) {
    throw 'Both x64 and x86 SCS position plugin source paths are required for installation.'
}
$sources = @{
    win_x64 = (Resolve-Path -LiteralPath $DllPath64 -ErrorAction Stop).Path
    win_x86 = (Resolve-Path -LiteralPath $DllPath32 -ErrorAction Stop).Path
}
$sourceHashes = @{}
foreach ($architecture in @('win_x64', 'win_x86')) {
    $sourceHashes[$architecture] =
        (Get-FileHash -LiteralPath $sources[$architecture] -Algorithm SHA256).Hash
}
New-Item -ItemType Directory -Path $BackupDirectory -Force | Out-Null

if ($SteamLibraryRoots -and $SteamLibraryRoots.Count -gt 0) {
    $libraries = @($SteamLibraryRoots | Where-Object {
        Test-Path -LiteralPath $_ -PathType Container
    } | ForEach-Object { Get-FullPath $_ })
} else {
    $libraries = @(Get-SteamLibraries)
}

$managedEntries = [System.Collections.Generic.List[object]]::new()
$byDestination = @{}
foreach ($entry in (Get-ManagedEntries)) {
    if (Test-ManagedDestination ([string]$entry.destination)) {
        $byDestination[(Get-FullPath ([string]$entry.destination))] = $entry
        $managedEntries.Add($entry)
    }
}

$destinations = [System.Collections.Generic.List[object]]::new()
foreach ($library in $libraries) {
    foreach ($gameFolder in @('Euro Truck Simulator 2', 'American Truck Simulator')) {
        $gameRoot = Join-Path $library "steamapps\common\$gameFolder"
        if (-not (Test-Path -LiteralPath $gameRoot -PathType Container)) {
            continue
        }
        foreach ($architecture in @('win_x64', 'win_x86')) {
            $architectureRoot = Join-Path $gameRoot "bin\$architecture"
            if (Test-Path -LiteralPath $architectureRoot -PathType Container) {
                $pluginDirectory = Join-Path $architectureRoot 'plugins'
                $destinations.Add([pscustomobject]@{
                    architecture = $architecture
                    path = Get-FullPath (Join-Path $pluginDirectory 'nlsi.dll')
                })
            }
        }
    }
}

if ($destinations.Count -eq 0) {
    Write-Status 'Install: no supported Steam ETS2/ATS game architecture was detected; no game files were changed.'
    exit 0
}

foreach ($candidate in $destinations) {
    $destination = [string]$candidate.path
    $architecture = [string]$candidate.architecture
    $sourceHash = [string]$sourceHashes[$architecture]
    $existingEntry = $byDestination[$destination]
    if (Test-Path -LiteralPath $destination -PathType Leaf) {
        $currentHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($currentHash -ieq $sourceHash -and -not $existingEntry) {
            Write-Status "Install: preserved an identical plugin not managed by this installer: $destination"
            continue
        }
        if ($existingEntry -and $currentHash -ine [string]$existingEntry.installed_sha256) {
            Write-Status "Install: preserved a user-modified managed plugin: $destination"
            continue
        }
        if (-not $existingEntry -and $currentHash -ieq $sourceHash) {
            continue
        }
    }

    $backupPath = ''
    $backupHash = ''
    if ($existingEntry) {
        $backupPath = [string]$existingEntry.backup_path
        $backupHash = [string]$existingEntry.backup_sha256
        if ($backupPath) {
            [void](Test-BackupPath $existingEntry)
        }
    } elseif (Test-Path -LiteralPath $destination -PathType Leaf) {
        $destinationHash = [System.Security.Cryptography.SHA256]::Create()
        try {
            $destinationKey = [BitConverter]::ToString(
                $destinationHash.ComputeHash([Text.Encoding]::UTF8.GetBytes($destination))).Replace('-', '')
        } finally {
            $destinationHash.Dispose()
        }
        $backupPath = Join-Path $BackupDirectory "$destinationKey-$([guid]::NewGuid().ToString('N')).bak"
        [System.IO.File]::Copy($destination, $backupPath, $false)
        $backupHash = (Get-FileHash -LiteralPath $backupPath -Algorithm SHA256).Hash
        Write-Status "Install: backed up the existing plugin to $backupPath"
    }

    $pluginDirectory = Split-Path -Parent $destination
    New-Item -ItemType Directory -Path $pluginDirectory -Force | Out-Null
    $temporary = "$destination.nlsi-$([guid]::NewGuid().ToString('N')).tmp"
    $replacementBackup = "$destination.nlsi-$([guid]::NewGuid().ToString('N')).bak"
    try {
        [System.IO.File]::Copy($sources[$architecture], $temporary, $false)
        if (Test-Path -LiteralPath $destination -PathType Leaf) {
            [System.IO.File]::Replace($temporary, $destination, $replacementBackup)
            Remove-Item -LiteralPath $replacementBackup -Force
        } else {
            [System.IO.File]::Move($temporary, $destination)
        }
    } finally {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) {
            Remove-Item -LiteralPath $temporary -Force
        }
        if (Test-Path -LiteralPath $replacementBackup -PathType Leaf) {
            Remove-Item -LiteralPath $replacementBackup -Force
        }
    }

    $installedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    if ($installedHash -ine $sourceHash) {
        throw "Installed SCS position plugin did not match its source: $destination"
    }
    $newEntry = [pscustomobject]@{
        destination = $destination
        installed_sha256 = $installedHash
        backup_path = $backupPath
        backup_sha256 = $backupHash
    }
    if ($existingEntry) {
        $managedEntries.Remove($existingEntry)
    }
    $managedEntries.Add($newEntry)
    $byDestination[$destination] = $newEntry
    Save-ManagedEntries $managedEntries
    Write-Status "Install: installed $architecture SCS position plugin: $destination"
}
