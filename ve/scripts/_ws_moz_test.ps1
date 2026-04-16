$ErrorActionPreference = "Stop"
$uri = [Uri]"ws://127.0.0.1:12100/"
$ws = New-Object System.Net.WebSockets.ClientWebSocket
$null = $ws.ConnectAsync($uri, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
$moz = ':c::save:{"id":99,"body":["json","/ve"]}'
$bytes = [Text.Encoding]::UTF8.GetBytes($moz)
$null = $ws.SendAsync([ArraySegment[byte]]::new($bytes), [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
$buf = [byte[]]::new(65536)
for ($i = 0; $i -lt 2; $i++) {
    $ms = New-Object System.IO.MemoryStream
    do {
        $r = $ws.ReceiveAsync([ArraySegment[byte]]::new($buf), [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        if ($r.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) { break }
        $ms.Write($buf, 0, $r.Count)
    } while (-not $r.EndOfMessage)
    Write-Host ("FRAME$($i+1): " + [Text.Encoding]::UTF8.GetString($ms.ToArray()))
}
if ($ws.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
    $null = $ws.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "", [Threading.CancellationToken]::None).GetAwaiter().GetResult()
}
$ws.Dispose()
