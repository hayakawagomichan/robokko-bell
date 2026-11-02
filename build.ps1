param(
    [switch]$Upload,
    [string]$Port
)
$ErrorActionPreference = 'Stop'
if ($Upload -and [string]::IsNullOrWhiteSpace($Port)) {
    throw 'Specify the device port: ./build.ps1 -Upload -Port COM3'
}
if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
    throw 'Install Arduino CLI first. See docs/development.md.'
}
$bellSketch = Join-Path $PSScriptRoot 'AtomChristmasBell'
$bellBuild = Join-Path $PSScriptRoot 'build'
& arduino-cli compile --fqbn esp32:esp32:pico32 --build-path $bellBuild $bellSketch
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
if ($Upload) {
    & arduino-cli upload --fqbn esp32:esp32:pico32 --port $Port --input-dir $bellBuild --upload-property upload.speed=115200 $bellSketch
    if ($LASTEXITCODE -ne 0) { throw 'Upload failed.' }
}
