$devices = @("2-1", "2-2")

# Ensure WSL is running
wsl -d Ubuntu true
Start-Sleep -Seconds 2

foreach ($busid in $devices) {
    try {
        usbipd attach --wsl --busid $busid
    } catch {
        Write-Host "Failed to attach $busid"
    }
}

# run via
# powershell -ExecutionPolicy Bypass -File attach-to-wsl.ps1