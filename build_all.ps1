# Build every example into its own build directory, so ONE command produces all
# the app binaries. ESP-IDF links exactly one app_main per image, so "build all"
# means one binary per example (x backend) -- not a single combined image.
#
# Each config goes to builds/<example>_toe<toe>/ (own -B dir), so no fullclean is
# needed between configs and the normal `idf.py build` in build/ is left alone.
# (The per-config dirs live under builds/, NOT under build/ -- a build dir nested
# inside another build dir breaks ESP-IDF's linker-script generation.)
#
# Run inside an ESP-IDF environment (idf.py on PATH):
#     . D:\Project\ESP32\esp-idf\export.ps1
#     .\build_all.ps1                      # every example, both backends (TOE 1 and 0)
#     .\build_all.ps1 -Toe 1               # every example, TOE=1 only
#     .\build_all.ps1 -Examples loopback   # one example, both backends
#
# Output binary: builds/<example>_toe<toe>/hello_world.bin
param(
    [int[]]$Toe = @(1, 0),
    [string[]]$Examples
)

$root = $PSScriptRoot

# Ensure idf.py is available. If not, this was likely launched from a plain
# terminal instead of the ESP-IDF one -- try to activate ESP-IDF via IDF_PATH,
# otherwise fail fast with a clear message (rather than N cryptic build failures).
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    $exp = if ($env:IDF_PATH) { Join-Path $env:IDF_PATH "export.ps1" } else { $null }
    if ($exp -and (Test-Path $exp)) {
        Write-Host "Activating ESP-IDF from $env:IDF_PATH ..." -ForegroundColor Cyan
        . $exp | Out-Null
    }
}
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: idf.py not found -- ESP-IDF environment is not active." -ForegroundColor Red
    Write-Host "  Open the ESP-IDF terminal (VS Code: 'ESP-IDF: Open ESP-IDF Terminal') and re-run," -ForegroundColor Red
    Write-Host "  or source export.ps1 first, e.g.:" -ForegroundColor Red
    Write-Host "      . D:\Project\ESP32\esp-idf\export.ps1" -ForegroundColor Red
    Write-Host "      .\build_all.ps1" -ForegroundColor Red
    exit 1
}

# Auto-discover examples: any examples/<name>/ that is a component (has CMakeLists.txt).
if (-not $Examples) {
    $Examples = Get-ChildItem -Path (Join-Path $root "examples") -Directory |
        Where-Object { Test-Path (Join-Path $_.FullName "CMakeLists.txt") } |
        Select-Object -ExpandProperty Name | Sort-Object
}

Write-Host "Examples: $($Examples -join ', ')   Backends (WIZNET_TOE): $($Toe -join ', ')" -ForegroundColor Cyan

$results = @()
foreach ($ex in $Examples) {
    # NOTE: the loop var must NOT be `$toe` — PowerShell vars are case-insensitive,
    # so `$toe` would alias the `$Toe` collection and the inner loop would clobber
    # it to a scalar (silently building only some configs). `$toeVal` is distinct.
    foreach ($toeVal in $Toe) {
        $bdir = Join-Path $root "builds/${ex}_toe${toeVal}"
        Write-Host "`n=== $ex  (WIZNET_TOE=$toeVal)  ->  $bdir ===" -ForegroundColor Yellow
        # Quote the -D args: bare `-DEXAMPLE=$ex` is parsed as a switch and does
        # NOT expand $ex in Windows PowerShell; a double-quoted string does.
        idf.py -B $bdir "-DEXAMPLE=$ex" "-DWIZNET_TOE=$toeVal" build
        $res = if ($LASTEXITCODE -eq 0) { "OK" } else { "FAIL" }
        $results += [pscustomobject]@{ Example = $ex; WIZNET_TOE = $toeVal; Result = $res }
    }
}

Write-Host "`n==================== SUMMARY ====================" -ForegroundColor Cyan
$results | Format-Table -AutoSize | Out-String | Write-Host
if ($results.Result -contains "FAIL") {
    Write-Host "Some builds FAILED." -ForegroundColor Red
    exit 1
}
Write-Host "All builds OK." -ForegroundColor Green
