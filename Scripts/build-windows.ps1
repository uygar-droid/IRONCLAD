# Local Windows build of IRONCLAD.vst3 with Visual Studio 2022.
# Requirements: Visual Studio 2022 (Desktop C++), CMake >= 3.25, Ninja, Git for Windows (bash).
# Usage (PowerShell, from the repository root):
#   .\Scripts\build-windows.ps1            # bootstrap + build + tests
#   .\Scripts\build-windows.ps1 -Validate  # ... + Steinberg validator
param([switch]$Validate)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$bash = "C:\Program Files\Git\bin\bash.exe"
if (-not (Test-Path $bash)) { throw "Git for Windows (bash) not found at $bash" }

& $bash Scripts/bootstrap.sh $(if ($Validate) { "validator" } else { "" })
if ($LASTEXITCODE -ne 0) { throw "bootstrap failed" }

cmake --preset windows-vs2022
if ($LASTEXITCODE -ne 0) { throw "configure failed" }
cmake --build --preset windows-vs2022-release
if ($LASTEXITCODE -ne 0) { throw "build failed" }
ctest --preset windows-vs2022-release
if ($LASTEXITCODE -ne 0) { throw "tests failed" }

$bundle = "build\windows-vs2022\out\IRONCLAD.vst3"
Write-Host "Built: $bundle"
if ($Validate) {
  & $bash Scripts/validate.sh $bundle
  if ($LASTEXITCODE -ne 0) { throw "VST3 validation failed" }
}
