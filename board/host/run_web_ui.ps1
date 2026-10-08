param(
    [switch]$DownloadYolo,
    [switch]$BuildOnly
)

$ErrorActionPreference = "Stop"

$repository = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

$installedOutput = & wsl.exe --list --quiet 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "WSL is not installed correctly:" -ForegroundColor Red
    Write-Host ($installedOutput | Out-String)
    Write-Host "Run from an elevated PowerShell: wsl --install -d Ubuntu"
    exit 1
}
$installed = @($installedOutput | ForEach-Object { ($_.ToString() -replace "`0", "").Trim() } |
    Where-Object { $_ })
if ($installed.Count -eq 0) {
    Write-Host "WSL is enabled, but no Linux distribution is installed." -ForegroundColor Red
    Write-Host "Run from an elevated PowerShell: wsl --install -d Ubuntu"
    exit 1
}
$distribution = if ($installed -contains "Ubuntu") { "Ubuntu" } else { $installed[0] }

$probe = & wsl.exe -d $distribution -- /bin/true 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "WSL distribution '$distribution' cannot start." -ForegroundColor Red
    Write-Host ($probe | Out-String)
    if (($probe | Out-String) -match "ERROR_PATH_NOT_FOUND") {
        Write-Host "The distribution registration or virtual disk path is missing." -ForegroundColor Yellow
        Write-Host "Try first:"
        Write-Host "  wsl --shutdown"
        Write-Host "  wsl --update"
        Write-Host "  wsl --list --verbose"
        Write-Host "If the distribution is disposable, reinstall it (THIS DELETES ITS DATA):"
        Write-Host "  wsl --unregister $distribution"
        Write-Host "  wsl --install -d $distribution"
        exit 1
    }
    Write-Host "Enable WSL2 from an elevated PowerShell:" -ForegroundColor Yellow
    Write-Host "  dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart"
    Write-Host "  dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart"
    Write-Host "  wsl --install -d Ubuntu"
    Write-Host "  bcdedit /set hypervisorlaunchtype auto"
    Write-Host "Then enable CPU virtualization in BIOS/UEFI and reboot Windows."
    Write-Host "If virtualization is unavailable, use WSL1:" -ForegroundColor Yellow
    Write-Host "  wsl --set-default-version 1"
    Write-Host "  wsl --set-version Ubuntu 1   # if Ubuntu already exists"
    Write-Host "  wsl --install -d Ubuntu"
    exit 1
}

$wslRepository = (& wsl.exe -d $distribution -- wslpath -a $repository 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or -not $wslRepository) {
    throw "The repository path cannot be converted by wslpath: $wslRepository"
}

Write-Host "Starting SeeSharp host UI in WSL..."
Write-Host "Distribution: $distribution"
Write-Host "Open http://127.0.0.1:8081/"
$arguments = ""
if ($DownloadYolo) { $arguments += " --download-yolo" }
if ($BuildOnly) { $arguments += " --build-only" }
& wsl.exe -d $distribution -- bash -lc "cd '$wslRepository' && exec sh board/host/run_web_ui.sh$arguments"
exit $LASTEXITCODE
