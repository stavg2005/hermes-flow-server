# 1. Try to get the Wi-Fi IP first (Online Mode)
try {
    $TargetIP = (Get-NetIPAddress -AddressFamily IPv4 -InterfaceAlias "Wi-Fi" -ErrorAction Stop).IPAddress
    $Mode = "ONLINE (Wi-Fi)"
} catch {
    # 2. If Wi-Fi fails, fallback to WSL IP (Offline Mode)
    Write-Warning "Wi-Fi not found or disconnected. Switching to Offline Mode..."
    try {
        # Finds adapter starting with "vEthernet (WSL" because the name varies
        $TargetIP = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.InterfaceAlias -like "vEthernet (WSL*"} | Select-Object -First 1).IPAddress
        $Mode = "OFFLINE (Internal WSL)"
    } catch {
        Write-Error "Could not find WSL adapter either! Are you running Docker?"
        exit
    }
}

# 3. Define the config path
$ConfigPath = ".\janus_conf\janus.jcfg"

# 4. Read the file and update the IP mapping
$Content = Get-Content $ConfigPath
$NewContent = $Content -replace 'nat_1_1_mapping = ".*"', "nat_1_1_mapping = `"$TargetIP`""
Set-Content -Path $ConfigPath -Value $NewContent

# 5. Restart Janus
Write-Host "----------------------------------------------------------------" -ForegroundColor Cyan
Write-Host "Mode: $Mode" -ForegroundColor Yellow
Write-Host "Updated janus.jcfg with IP: $TargetIP" -ForegroundColor Green
Write-Host "Restarting Janus Container..." -ForegroundColor Yellow
Write-Host "----------------------------------------------------------------" -ForegroundColor Cyan

docker compose restart janus

# 6. Print the exact commands
Write-Host "DONE! Ready." -ForegroundColor Green
Write-Host ""
Write-Host "1. USE THIS URL IN REACT/BROWSER:" -ForegroundColor Yellow
Write-Host "   http://$($TargetIP):8088/janus" -ForegroundColor White
Write-Host ""
Write-Host "2. RUN THIS FFMPEG COMMAND:" -ForegroundColor Yellow
# Note: In Offline mode, the TargetIP IS the WSL IP, so we use it for both.
Write-Host "   ffmpeg -re -stream_loop -1 -i music.mp3 -c:a pcm_alaw -ar 8000 -ac 1 -f rtp -payload_type 8 udp://$($TargetIP):10050" -ForegroundColor White
Write-Host "----------------------------------------------------------------" -ForegroundColor Cyan
