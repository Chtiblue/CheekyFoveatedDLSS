param([string]$Label='nr-bridge-preview1')
$ErrorActionPreference='Stop'
if($Label -notmatch '^[a-zA-Z0-9.-]+$'){throw 'Invalid label'}
$repo=Split-Path -Parent $PSScriptRoot
$bin=Join-Path $repo 'bin\Release'
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach($mode in @('UEVR','Standalone')) {
 $stage=Join-Path $repo ('build\nr-release-'+$mode+'-'+[guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory -Path $stage | Out-Null
 $files=[ordered]@{'START-HERE.txt'=(Join-Path $repo 'NR-BRIDGE-START-HERE.txt');'licenses\Cheeky-GPLv3.txt'=(Join-Path $repo 'LICENSE');'licenses\MinHook.txt'=(Join-Path $repo 'third_party\reshade\deps\minhook\LICENSE.txt');'licenses\OpenVR.txt'=(Join-Path $repo 'third_party\openvr\LICENSE')}
 if($mode -eq 'UEVR') {
  $files['plugins\CheekyFoveatedDLSS.dll']=Join-Path $bin 'CheekyFoveatedDLSS.dll'
  $files['plugins\CheekyFoveatedDLSS\CheekyFoveatedDLSSRuntime.dll']=Join-Path $bin 'CheekyFoveatedDLSS\CheekyFoveatedDLSSRuntime.dll'
  $files['scripts\cheeky_foveated_dlss.lua']=Join-Path $repo 'uevr\scripts\cheeky_foveated_dlss.lua'
  $files['licenses\UEVR-API-MIT.txt']=Join-Path $repo 'third_party\uevr\LICENSE.txt'
 } else {
  foreach($name in @('CheekyFoveatedDLSSHost.dll','CheekyFoveatedDLSSRuntime.dll')){$files['CheekyFoveatedDLSS\'+$name]=Join-Path $bin ('CheekyFoveatedDLSS\'+$name)}
  $files['licenses\DearImGui.txt']=Join-Path $repo 'third_party\reshade\deps\imgui\LICENSE.txt'
 }
 $hashes=@()
 foreach($entry in $files.GetEnumerator()) {
  $dest=Join-Path $stage $entry.Key
  New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force | Out-Null
  Copy-Item -LiteralPath $entry.Value -Destination $dest
  $hashes+=((Get-FileHash -LiteralPath $dest).Hash.ToLowerInvariant()+'  '+$entry.Key.Replace('\','/'))
 }
 [IO.File]::WriteAllText((Join-Path $stage 'SHA256SUMS.txt'),($hashes -join "`n")+"`n",(New-Object Text.UTF8Encoding($false)))
 $archive=Join-Path $bin ('Cheeky-0.5.5-'+$mode+'-Upgrade-'+$Label+'.zip')
 [IO.Compression.ZipFile]::CreateFromDirectory($stage,$archive)
 $zip=[IO.Compression.ZipFile]::OpenRead($archive)
 try {
  $payload=@($zip.Entries | Where-Object {!$_.FullName.EndsWith('/')})
  if($payload.Count -ne $files.Count+1){throw 'Unexpected ZIP entries'}
  foreach($entry in $payload){
   $name=$entry.FullName.Replace('/','\')
   if($name -ne 'SHA256SUMS.txt' -and !$files.Contains($name)){throw "Unexpected ZIP entry $name"}
   $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
   try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
   if($hash -ne (Get-FileHash -LiteralPath (Join-Path $stage $name)).Hash){throw "Bad ZIP hash $name"}
  }
 } finally {$zip.Dispose()}
 Get-FileHash -LiteralPath $archive
}
