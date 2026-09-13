$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "C:\Users\Admin\Downloads\release\Release\bin\project_x.exe"
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true

$proc = [System.Diagnostics.Process]::Start($psi)
$stdout = $proc.StandardOutput.ReadToEnd()
$stderr = $proc.StandardError.ReadToEnd()
$proc.WaitForExit()

Write-Host "=== STDOUT ==="
Write-Host $stdout
Write-Host "=== STDERR ==="
Write-Host $stderr
Write-Host "=== EXIT CODE ==="
Write-Host $proc.ExitCode
