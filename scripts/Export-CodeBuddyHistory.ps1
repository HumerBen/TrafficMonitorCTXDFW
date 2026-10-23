<#
.SYNOPSIS
  Export a CodeBuddy local chat session to a readable Markdown file (for restoring/keeping history).
.DESCRIPTION
  CodeBuddy stores chat history locally under:
    %LOCALAPPDATA%\CodeBuddyExtension\Data\<userHash>\VisualStudio\<userHash>\history\<workspace>\<conversation>\messages\<id>.json
  Each conversation folder has index.json (message order) and messages/ (one file per message).
  This script reads a given conversation and arranges user/assistant/tool content into Markdown.
.PARAMETER ConvDir
  Full path to the conversation folder (the one containing index.json).
.PARAMETER OutFile
  Output Markdown file path.
.PARAMETER MaxToolResult
  Max chars kept for each tool result (truncated if longer). Default 4000.
#>
param(
    [string]$ConvDir = "C:\Users\wcmco\AppData\Local\CodeBuddyExtension\Data\ac6ce578-7c42-46f0-a72a-e0a491a4be9b\VisualStudio\ac6ce578-7c42-46f0-a72a-e0a491a4be9b\history\f2c884c7f4c97c6abe8d29fdbdb29587\9d5499ea60c8426ea497feec20a9b843",
    [string]$OutFile = "d:\CodeProjects\TrafficMonitorCTXDFW\ChatHistoryExport\recover_history_session.md",
    [int]$MaxToolResult = 4000
)

$ErrorActionPreference = 'SilentlyContinue'

if (-not (Test-Path (Join-Path $ConvDir 'index.json'))) {
    Write-Error "Cannot find conversation index.json: $ConvDir"
    exit 1
}

$index = Get-Content (Join-Path $ConvDir 'index.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$order = $index.messages
if (-not $order) { Write-Error "index.json has no messages order list"; exit 1 }

$out = [System.Text.StringBuilder]::new()
[void]$out.AppendLine("# CodeBuddy Session History Export")
[void]$out.AppendLine()
$convId = if ($index.current) { [string]$index.current } else { Split-Path $ConvDir -Leaf }
[void]$out.AppendLine("- Conversation ID: $convId")
[void]$out.AppendLine("- Exported at: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
[void]$out.AppendLine("- Message count: $($order.Count)")
[void]$out.AppendLine()

function Extract-UserText($blocks) {
    $t = ($blocks | Where-Object { $_.type -eq 'text' } | ForEach-Object { $_.text }) -join "`n"
    if ($t -match '<user_query>\s*(.*?)\s*</user_query>') { return $Matches[1].Trim() }
    return $t
}

function Extract-AssistantText($blocks) {
    return ($blocks | Where-Object { $_.type -eq 'text' } | ForEach-Object { $_.text }) -join "`n"
}

function Extract-ToolResult($block) {
    $r = $block.result
    $txt = ''
    if ($r -and $r.result -and $r.result.content) { $txt = [string]$r.result.content }
    elseif ($r -and $r.content) { $txt = [string]$r.content }
    elseif ($r -and $r.PSObject.Properties['result']) { $txt = [string]$r.result }
    else { $txt = ($r | ConvertTo-Json -Compress -Depth 3) }
    if ($txt.Length -gt $MaxToolResult) {
        $txt = $txt.Substring(0, $MaxToolResult) + "`n`n...[truncated, original length $($txt.Length) chars]"
    }
    return $txt
}

foreach ($entry in $order) {
    $mf = Join-Path $ConvDir "messages\$($entry.id).json"
    if (-not (Test-Path $mf)) { continue }
    $raw = Get-Content $mf -Raw -Encoding UTF8 | ConvertFrom-Json
    $role = [string]$raw.role
    try { $msg = $raw.message | ConvertFrom-Json } catch { $msg = $null }
    if ($null -eq $msg) { continue }
    $blocks = $msg.content
    if ($blocks -isnot [System.Collections.IEnumerable] -or $blocks -is [string]) { $blocks = @() }

    if ($role -eq 'user') {
        [void]$out.AppendLine("## User")
        [void]$out.AppendLine()
        [void]$out.AppendLine((Extract-UserText $blocks))
        [void]$out.AppendLine()
    }
    elseif ($role -eq 'assistant') {
        $visible = Extract-AssistantText $blocks
        [void]$out.AppendLine("## Assistant")
        [void]$out.AppendLine()
        if ($visible) { [void]$out.AppendLine($visible); [void]$out.AppendLine() }
        $calls = $blocks | Where-Object { $_.type -eq 'tool-call' }
        foreach ($c in $calls) {
            $argsJson = ($c.args | ConvertTo-Json -Compress -Depth 3)
            if ($argsJson.Length -gt 500) { $argsJson = $argsJson.Substring(0,500) + '...' }
            [void]$out.AppendLine("> Tool call: **$($c.toolName)**  ``($argsJson)``")
        }
    }
    elseif ($role -eq 'tool') {
        [void]$out.AppendLine("### Tool Result")
        [void]$out.AppendLine()
        [void]$out.AppendLine('```')
        foreach ($b in $blocks) {
            if ($b.type -eq 'tool-result') { [void]$out.AppendLine((Extract-ToolResult $b)) }
        }
        [void]$out.AppendLine('```')
        [void]$out.AppendLine()
    }
}

$dir = Split-Path $OutFile -Parent
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }

Set-Content -Path $OutFile -Value $out.ToString() -Encoding UTF8
Write-Host "Export done -> $OutFile  ($([math]::Round((Get-Item $OutFile).Length/1KB,1)) KB)"
