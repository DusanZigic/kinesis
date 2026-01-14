param (
    [switch]$Release
)

$Compiler    = "g++"
$IncludePath = "-Iinclude"
$Source      = Get-ChildItem -Path "src" -Filter *.cpp | ForEach-Object { $_.FullName }
$ResourceSrc = "resources.rc"
$ResourceObj = "resources.res"

$LDLibs = @(
    "-lgdi32",
    "-lcomctl32",
    "-ldwmapi",
    "-luser32",
    "-lpsapi",
    "-lshell32",
    "-lole32",
    "-lshlwapi",
    "-luuid",
    "-pthread",
    "-lgdiplus"
)

if ($Release) {
    $TargetDir = "bin"
    $Target    = "$TargetDir/ks.exe"
    if (!(Test-Path $TargetDir)) { New-Item -ItemType Directory -Path $TargetDir }

    Write-Host "Building RELEASE Version..." -ForegroundColor Magenta

    & windres $ResourceSrc -O coff -o $ResourceObj

    $CXXFlags = @(
        "-std=c++17",
        "-O3",
        "-mwindows",
        "-static",
        "-s",
        "-Wall"
    )
    
    & $Compiler $Source $ResourceObj -o $Target $IncludePath $CXXFlags $LDLibs
} 
else {
    $Target = "ks.exe"
    Write-Host "Building DEBUG Version..." -ForegroundColor Cyan

    & windres $ResourceSrc -O coff -o $ResourceObj

    $CXXFlags = @(
        "-std=c++17",
        "-g",
        "-Wall",
        "-Wextra"
    )

    & $Compiler $Source $ResourceObj -o $Target $IncludePath $CXXFlags $LDLibs
}

if ($LASTEXITCODE -eq 0) {
    Write-Host "Build Successful!" -ForegroundColor Green
    if ($ResourceObj -and (Test-Path $ResourceObj)) { Remove-Item $ResourceObj }
} else {
    Write-Host "Build Failed with exit code $LASTEXITCODE" -ForegroundColor Red
}