param([Parameter(Mandatory=$true)][string]$Directory)
$ErrorActionPreference='Stop'
$Directory=(Resolve-Path -LiteralPath $Directory).Path
$executable=Join-Path $Directory 'poolrad.exe'
if(!(Test-Path -LiteralPath $executable -PathType Leaf)){throw "Missing release executable: $executable"}
$shell=New-Object -ComObject WScript.Shell
$shortcut=$null
try {
    $shortcut=$shell.CreateShortcut((Join-Path $Directory 'Pool of Radiance QOL.lnk'))
    $shortcut.TargetPath=$executable
    $shortcut.Arguments='--qol'
    $shortcut.WorkingDirectory=$Directory
    $shortcut.Description='Pool of Radiance with quality-of-life upgrades'
    $shortcut.IconLocation="$executable,0"
    $shortcut.WindowStyle=1
    $shortcut.Save()
    if($shortcut.TargetPath -ne $executable -or $shortcut.Arguments -ne '--qol' -or $shortcut.WorkingDirectory -ne $Directory){throw 'Release shortcut verification failed'}
} finally {
    if($null -ne $shortcut){[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut)}
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)
}
