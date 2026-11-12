# Shows a test achievement card from the running launcher over whatever is in front (a game too):
#   tools\achievement-toast-test.ps1 [-Name "Some achievement"] [-Delay 5]
# -Delay waits first, to switch to the game. Without a running launcher it starts one.
param(
    [string]$Name = "",
    [int]$Delay = 0,
    [string]$Exe = (Join-Path $PSScriptRoot "..\build\vs\launchme.exe")
)
if ($Delay -gt 0) { Start-Sleep -Seconds $Delay }
$command = if ($Name) { "achievement-test:$Name" } else { "achievement-test" }
& $Exe "--jump=$command"
