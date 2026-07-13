param(
  [string]$Picotool = "$env:USERPROFILE/.pico-sdk/picotool/2.2.0/picotool/picotool.exe"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Picotool)) {
  Write-Error "picotool was not found at '$Picotool'."
  exit 1
}

function Test-BootselReady {
  & $Picotool info *> $null
  return $LASTEXITCODE -eq 0
}

function Wait-BootselReady([int]$TimeoutMilliseconds) {
  $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
  while ([DateTime]::UtcNow -lt $deadline) {
    if (Test-BootselReady) {
      return $true
    }
    Start-Sleep -Milliseconds 100
  }
  return $false
}

if (Test-BootselReady) {
  Write-Host "Prism is already in BOOTSEL mode."
  exit 0
}

# Prefer picotool's reset interface. It works without racing the CDC port and
# also handles firmware whose serial interface is currently in use.
& $Picotool reboot -f -u
if ($LASTEXITCODE -eq 0 -and (Wait-BootselReady 5000)) {
  Write-Host "Prism entered BOOTSEL mode through picotool."
  exit 0
}

# Fall back to the standard 1200-baud CDC reset for firmware without an
# accessible picotool reset interface.
$deadline = [DateTime]::UtcNow.AddSeconds(10)
while ([DateTime]::UtcNow -lt $deadline) {
  if (Test-BootselReady) {
    Write-Host "Prism is in BOOTSEL mode."
    exit 0
  }

  $device = Get-CimInstance Win32_SerialPort |
    Where-Object { $_.PNPDeviceID -match 'VID_2E8A&PID_000A&MI_00' } |
    Select-Object -First 1
  if ($null -eq $device) {
    Start-Sleep -Milliseconds 100
    continue
  }

  $port = [System.IO.Ports.SerialPort]::new($device.DeviceID, 1200)
  try {
    $port.DtrEnable = $true
    $port.Open()
    Start-Sleep -Milliseconds 100
  } catch {
    # The device may disappear between enumeration and Open(). Keep polling.
  } finally {
    if ($port.IsOpen) {
      $port.Close()
    }
    $port.Dispose()
  }

  if (Wait-BootselReady 3000) {
    Write-Host "Prism entered BOOTSEL mode through its CDC port."
    exit 0
  }
}

Write-Error "Prism did not enter BOOTSEL mode. Check its USB connection and try again."
exit 1
