$ErrorActionPreference = "Stop"

$deadline = [DateTime]::UtcNow.AddSeconds(15)
while ([DateTime]::UtcNow -lt $deadline) {
  $device = Get-CimInstance Win32_SerialPort |
    Where-Object { $_.PNPDeviceID -match 'VID_2E8A&PID_000A&MI_00' } |
    Select-Object -First 1
  if ($null -eq $device) {
    Start-Sleep -Milliseconds 50
    continue
  }

  $port = [System.IO.Ports.SerialPort]::new($device.DeviceID, 1200)
  try {
    $port.DtrEnable = $true
    $port.Open()
    Start-Sleep -Milliseconds 100
    Write-Host "Asked Prism on $($device.DeviceID) to enter BOOTSEL mode."
    Start-Sleep -Milliseconds 800
    exit 0
  } catch {
    # A rebooting device can disappear between enumeration and Open(). Keep
    # polling until one complete CDC line-coding request gets through.
    Start-Sleep -Milliseconds 50
  } finally {
    if ($port.IsOpen) { $port.Close() }
    $port.Dispose()
  }
}

Write-Host "Prism CDC port was not caught during the recovery window; it may already be in BOOTSEL mode."
exit 0
