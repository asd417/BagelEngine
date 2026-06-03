param([switch]$DebugOnly, [switch]$ReleaseOnly, [switch]$NoDeps)

$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
$root    = $PSScriptRoot
$vcxproj = "$root\build\BagelEngine.vcxproj"
$joltVcx = "$root\Dependencies\JoltPhysics\Build\VS2022_CL\Jolt.vcxproj"
$ktxSln  = "$root\Dependencies\KTX-Software\build\lib\libktx.sln"

if (-not (Test-Path $vcxproj)) {
    Write-Host "[build] Project not found. Run setup.bat first." -ForegroundColor Red
    exit 1
}

# Launch an MSBuild process and return the Process object.
# Uses System.Diagnostics.Process directly — Start-Process -PassThru has a PS 5.1
# bug where ExitCode is always null even after WaitForExit().
function Start-MSBuild($proj, $cfg, $logFile) {
    $psi = [System.Diagnostics.ProcessStartInfo]::new($msbuild)
    $psi.Arguments    = "`"$proj`" /p:Configuration=$cfg /p:Platform=x64 /m /nologo /flp:logfile=`"$logFile`";verbosity=minimal"
    $psi.UseShellExecute = $false  # required for reliable ExitCode
    return [System.Diagnostics.Process]::Start($psi)
}

function Wait-AndReport($label, $proc, $logFile) {
    $proc.WaitForExit()
    if ($proc.ExitCode -eq 0) {
        Write-Host "[build] $label : OK" -ForegroundColor Green
        return $true
    }
    Write-Host "[build] $label : FAILED (see $logFile)" -ForegroundColor Red
    Get-Content $logFile -ErrorAction SilentlyContinue |
        Where-Object { $_ -match ": (fatal )?error [A-Z]" } |
        ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    return $false
}

# One-time dep builds (Release libs only — Debug is pre-built)
if (-not $DebugOnly -and -not $NoDeps) {
    if (-not (Test-Path "$root\Dependencies\JoltPhysics\Build\VS2022_CL\Release\Jolt.lib")) {
        Write-Host "[build] Building Jolt Release (one-time)..." -ForegroundColor Cyan
        $p = Start-MSBuild $joltVcx "Release" "$root\build\jolt_release.log"
        if (-not (Wait-AndReport "Jolt Release" $p "$root\build\jolt_release.log")) { exit 1 }
    }
    if (-not (Test-Path "$root\Dependencies\KTX-Software\build\lib\Release\ktx.lib")) {
        Write-Host "[build] Building KTX Release (one-time)..." -ForegroundColor Cyan
        $p = Start-MSBuild $ktxSln "Release" "$root\build\ktx_release.log"
        if (-not (Wait-AndReport "KTX Release" $p "$root\build\ktx_release.log")) { exit 1 }
    }
}

$configs = @()
if (-not $ReleaseOnly) { $configs += "Debug"   }
if (-not $DebugOnly)   { $configs += "Release" }

Write-Host "[build] Starting: $($configs -join ' + ')" -ForegroundColor Cyan

$jobs = @{}
foreach ($cfg in $configs) {
    $log  = "$root\build\$($cfg.ToLower()).log"
    $proc = Start-MSBuild $vcxproj $cfg $log
    $jobs[$cfg] = @{ Proc = $proc; Log = $log }
    Write-Host "[build] $cfg started (PID $($proc.Id))"
}

Write-Host ""
$anyFailed = $false
foreach ($cfg in $configs) {
    if (-not (Wait-AndReport $cfg $jobs[$cfg].Proc $jobs[$cfg].Log)) {
        $anyFailed = $true
    }
}

exit ([int]$anyFailed)
