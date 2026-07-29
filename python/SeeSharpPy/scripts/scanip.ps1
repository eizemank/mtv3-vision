# В PowerShell — найти все активные устройства в подсети
1..254 | ForEach-Object {
    $ip = "192.168.0.$_"
    if (Test-Connection -ComputerName $ip -Count 1 -Quiet -TimeoutSeconds 1) {
        Write-Host "$ip online"
    }
}