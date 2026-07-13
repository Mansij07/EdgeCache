# Windows-friendly smoke test for a running EdgeCache stack.
#   powershell -File scripts\smoke.ps1
$ErrorActionPreference = 'Stop'

$proxies = @('http://localhost:8080', 'http://localhost:8082', 'http://localhost:8083')
$control = 'http://localhost:9000'
$adminKey = if ($env:ADMIN_API_KEY) { $env:ADMIN_API_KEY } else { 'dev-admin-key' }
$path = '/products/smoke-1'

function Get-XCache($base) {
    $r = Invoke-WebRequest -Uri "$base$path" -Method GET -UseBasicParsing
    return $r.Headers['X-Cache']
}

Write-Host "1) First request to proxy1 (expect MISS):" -ForegroundColor Cyan
Write-Host "   X-Cache =" (Get-XCache $proxies[0])
Write-Host "2) Second request to proxy1 (expect HIT):" -ForegroundColor Cyan
Write-Host "   X-Cache =" (Get-XCache $proxies[0])

Write-Host "3) Purge across the fleet..." -ForegroundColor Cyan
$headers = @{ Authorization = "Bearer $adminKey"; 'Content-Type' = 'application/json' }
$body = (@{ pattern = $path } | ConvertTo-Json)
$resp = Invoke-RestMethod -Uri "$control/purge" -Method POST -Headers $headers -Body $body
Write-Host "   ->" ($resp | ConvertTo-Json -Compress)

Start-Sleep -Seconds 1

Write-Host "4) Every replica should MISS after purge:" -ForegroundColor Cyan
foreach ($p in $proxies) {
    Write-Host "   $p X-Cache =" (Get-XCache $p)
}

Write-Host "Smoke test complete." -ForegroundColor Green
