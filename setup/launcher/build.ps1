param(
  [Parameter(Mandatory = $true)][string]$Output,
  [Parameter(Mandatory = $true)][string]$Intermediate
)
$ErrorActionPreference = 'Stop'
$framework = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319'
$wpf = Join-Path $framework 'WPF'
$null = New-Item -ItemType Directory -Path $Intermediate, (Split-Path -Parent $Output) -Force
$resources = Join-Path $Intermediate 'AlienModSetup.g.resources'
$writer = [Resources.ResourceWriter]::new($resources)
try {
  foreach ($name in 'tunnel.jpg', 'BarlowSemiCondensed-Bold.ttf', 'launcher.ico') {
    $stream = [IO.File]::OpenRead((Join-Path $PSScriptRoot "assets\$name"))
    $writer.AddResource("assets/$($name.ToLowerInvariant())", $stream, $true)
  }
} finally {
  $writer.Dispose()
}
$layout = Join-Path $PSScriptRoot 'Launcher.xaml'
$strings = Join-Path (Split-Path -Parent $PSScriptRoot) 'strings.h'
$uiStrings = Join-Path $PSScriptRoot 'ui.tsv'
$manifest = Join-Path $PSScriptRoot 'launcher.manifest'
$license = Join-Path $PSScriptRoot 'assets\OFL.txt'
$icon = Join-Path $PSScriptRoot 'assets\launcher.ico'
$arguments = @(
  '/nologo', '/target:winexe', '/optimize+', '/codepage:65001', "/out:$Output",
  "/win32manifest:$manifest", "/win32icon:$icon", "/resource:$resources,AlienModSetup.g.resources",
  "/resource:$layout,Launcher.xaml", "/resource:$strings,strings.h", "/resource:$uiStrings,ui.tsv", "/resource:$license,Barlow-OFL.txt",
  "/reference:$wpf\PresentationCore.dll", "/reference:$wpf\PresentationFramework.dll",
  "/reference:$wpf\WindowsBase.dll", "/reference:$framework\System.Xaml.dll", '/reference:System.Windows.Forms.dll',
  (Join-Path $PSScriptRoot 'Launcher.cs'), (Join-Path $PSScriptRoot 'Radar.cs')
)
& "$framework\csc.exe" @arguments
exit $LASTEXITCODE
