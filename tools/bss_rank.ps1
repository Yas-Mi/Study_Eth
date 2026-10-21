param(
    [Parameter(Mandatory = $true)]
    [string]$MapFile
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $MapFile -PathType Leaf)) {
    throw "Map file not found: $MapFile"
}

$lines = Get-Content -LiteralPath $MapFile
$bssStart = -1
$bssSize = 0

for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^\.bss\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)') {
        $address = [Convert]::ToInt64($Matches[1].Substring(2), 16)
        if ($address -ne 0) {
            $bssStart = $i
            $bssSize = [Convert]::ToInt64($Matches[2].Substring(2), 16)
        }
    }
}

if ($bssStart -lt 0) {
    throw "Output .bss section not found in map file: $MapFile"
}

$bssEnd = -1
for ($i = $bssStart; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '_ebss\s*=\s*\.') {
        $bssEnd = $i
        break
    }
}

if ($bssEnd -lt 0) {
    throw "End of .bss section (_ebss) not found in map file: $MapFile"
}

$variables = [System.Collections.Generic.List[object]]::new()
for ($i = $bssStart + 1; $i -lt $bssEnd; $i++) {
    if ($lines[$i] -notmatch '^\s+\.bss\.(?<name>\S+)\s*(?<details>.*)$') {
        continue
    }

    $name = $Matches['name']
    $details = $Matches['details']
    if ($details -notmatch '0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)') {
        if (($i + 1) -ge $bssEnd -or
            $lines[$i + 1] -notmatch '^\s+0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)') {
            continue
        }
        $i++
    }

    $size = [Convert]::ToInt64($Matches[1].Substring(2), 16)
    if ($size -gt 0) {
        $variables.Add([PSCustomObject]@{
            Name = $name
            Size = $size
        })
    }
}

if ($variables.Count -eq 0) {
    throw "No BSS variables found in map file: $MapFile"
}

$sorted = @($variables | Sort-Object -Property @{ Expression = 'Size'; Descending = $true }, Name)
$mapName = [System.IO.Path]::GetFileName($MapFile)
Write-Output "BSS variable ranking ($mapName)"
Write-Output ("Total .bss: {0:N0} bytes ({1:N2} KiB)" -f $bssSize, ($bssSize / 1KB))
Write-Output ""

for ($i = 0; $i -lt $sorted.Count; $i++) {
    $item = $sorted[$i]
    Write-Output ("{0,3}. {1,-40} {2,10:N0} bytes ({3,7:N2} KiB)" -f
        ($i + 1), $item.Name, $item.Size, ($item.Size / 1KB))
}
