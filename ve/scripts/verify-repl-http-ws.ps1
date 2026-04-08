# Smoke-test VE HTTP / WebSocket / optional TCP REPL after starting ve.exe with ve/program/ve.json.
# Usage (from repo root):
#   .\ve\scripts\verify-repl-http-ws.ps1
#   .\ve\scripts\verify-repl-http-ws.ps1 -HttpPort 5080 -WsPort 5081 -ReplPort 5061

param(
    [int] $HttpPort = 5080,
    [int] $WsPort = 5081,
    [int] $ReplPort = 5061,
    [switch] $SkipRepl
)

$ErrorActionPreference = "Stop"

function Write-Step($msg) { Write-Host "`n=== $msg ===" -ForegroundColor Cyan }

Write-Step "HTTP GET /health"
$healthCode = curl.exe -s -o $env:TEMP\ve_health.txt -w "%{http_code}" "http://127.0.0.1:$HttpPort/health"
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAIL: curl exit $LASTEXITCODE (is ve.exe running?)" -ForegroundColor Red
    Write-Host "Example: cmake-build-release-vs2022\bin\ve.exe ve\program\ve.json"
    exit 1
}
Write-Host "HTTP status: $healthCode"
Get-Content $env:TEMP\ve_health.txt | Write-Host

Write-Step "HTTP GET /api/cmd (list commands)"
curl.exe -s "http://127.0.0.1:$HttpPort/api/cmd" | Write-Host

Write-Step "HTTP POST /api/cmd/save wait=false (expect 202 if command runs async on main loop)"
$bodyAsync = '{"args":["json","/ve"],"wait":false}'
$codeAsync = curl.exe -s -o $env:TEMP\ve_save_async.json -w "%{http_code}" `
    -X POST "http://127.0.0.1:$HttpPort/api/cmd/save" `
    -H "Content-Type: application/json" -d $bodyAsync
Write-Host "HTTP status: $codeAsync"
Get-Content $env:TEMP\ve_save_async.json | Write-Host

Write-Step "HTTP POST /api/cmd/save wait=true (expect 200 + result)"
$bodySync = '{"args":["json","/ve"],"wait":true}'
$codeSync = curl.exe -s -o $env:TEMP\ve_save_sync.json -w "%{http_code}" `
    -X POST "http://127.0.0.1:$HttpPort/api/cmd/save" `
    -H "Content-Type: application/json" -d $bodySync
Write-Host "HTTP status: $codeSync"
Get-Content $env:TEMP\ve_save_sync.json | Write-Host

Write-Step "WebSocket (Moz text): two frames accepted + result for command save"

$wsUri = [Uri]"ws://127.0.0.1:$WsPort/"
$ws = New-Object System.Net.WebSockets.ClientWebSocket
try {
    $ws.ConnectAsync($wsUri, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    $moz = ':c::save:{"id":42,"body":["json","/ve"]}'
    $bytes = [Text.Encoding]::UTF8.GetBytes($moz)
    $seg = [ArraySegment[byte]]::new($bytes)
    $ws.SendAsync($seg, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()

    $buf = [byte[]]::new(65536)
    for ($i = 0; $i -lt 2; $i++) {
        $ms = New-Object System.IO.MemoryStream
        do {
            $r = $ws.ReceiveAsync([ArraySegment[byte]]::new($buf), [Threading.CancellationToken]::None).GetAwaiter().GetResult()
            if ($r.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) { break }
            $ms.Write($buf, 0, $r.Count)
        } while (-not $r.EndOfMessage)
        $text = [Text.Encoding]::UTF8.GetString($ms.ToArray())
        Write-Host "WS frame $($i+1): $text"
    }
} finally {
    if ($ws.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
        $ws.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "", [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    }
    $ws.Dispose()
}

function Receive-WsText($websocket, [byte[]]$buffer) {
    $ms = New-Object System.IO.MemoryStream
    do {
        $rr = $websocket.ReceiveAsync([ArraySegment[byte]]::new($buffer), [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        if ($rr.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) { break }
        $ms.Write($buffer, 0, $rr.Count)
    } while (-not $rr.EndOfMessage)
    return [Text.Encoding]::UTF8.GetString($ms.ToArray())
}

Write-Step "WebSocket JSON command.run wait=false (accepted + result, veservice.js default)"
$ws2 = New-Object System.Net.WebSockets.ClientWebSocket
try {
    $null = $ws2.ConnectAsync($wsUri, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    $j = '{"cmd":"command.run","name":"save","args":["json","/ve"],"id":100,"wait":false}'
    $b2 = [Text.Encoding]::UTF8.GetBytes($j)
    $null = $ws2.SendAsync([ArraySegment[byte]]::new($b2), [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    Write-Host "WS JSON frame 1: $(Receive-WsText $ws2 $buf)"
    Write-Host "WS JSON frame 2: $(Receive-WsText $ws2 $buf)"
} finally {
    if ($ws2.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
        $null = $ws2.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "", [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    }
    $ws2.Dispose()
}

Write-Step "WebSocket JSON command.run wait=true (single type:ok)"
$ws3 = New-Object System.Net.WebSockets.ClientWebSocket
try {
    $null = $ws3.ConnectAsync($wsUri, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    $j3 = '{"cmd":"command.run","name":"save","args":["json","/ve"],"id":101,"wait":true}'
    $b3 = [Text.Encoding]::UTF8.GetBytes($j3)
    $null = $ws3.SendAsync([ArraySegment[byte]]::new($b3), [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    Write-Host "WS JSON reply: $(Receive-WsText $ws3 $buf)"
} finally {
    if ($ws3.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
        $null = $ws3.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "", [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    }
    $ws3.Dispose()
}

if (-not $SkipRepl) {
    Write-Step "TCP REPL (optional): echo help"
    try {
        $c = New-Object System.Net.Sockets.TcpClient("127.0.0.1", $ReplPort)
        $st = $c.GetStream()
        $line = [Text.Encoding]::UTF8.GetBytes("help`n")
        $st.Write($line, 0, $line.Length)
        $st.Flush()
        Start-Sleep -Milliseconds 200
        $rb = New-Object byte[] 4096
        $n = $st.Read($rb, 0, $rb.Length)
        if ($n -gt 0) {
            Write-Host ([Text.Encoding]::UTF8.GetString($rb, 0, [Math]::Min($n, 2000)))
        } else {
            Write-Host "(no data; REPL may use different protocol or disabled)"
        }
        $c.Close()
    } catch {
        Write-Host "REPL skip: $($_.Exception.Message)" -ForegroundColor Yellow
    }
}

Write-Host "`nDone." -ForegroundColor Green
