$Compiler    = "g++"
$Target      = "ks.exe"
$Source      = Get-ChildItem -Path "src" -Filter *.cpp | ForEach-Object { $_.FullName }
$IncludePath = "-Iinclude"

$CXXFlags = @(
    "-std=c++17",
    "-O2",
    "-mwindows",
    "-Wall",
    "-Wextra"
)

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
    "-pthread"
)

Write-Host "Building $Target..." -ForegroundColor Cyan

& $Compiler $Source -o $Target $IncludePath $CXXFlags $LDLibs

if ($LASTEXITCODE -eq 0) {
    Write-Host "Build Successful!" -ForegroundColor Green
} else {
    Write-Host "Build Failed with exit code $LASTEXITCODE" -ForegroundColor Red
}