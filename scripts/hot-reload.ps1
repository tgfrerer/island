if ($Env:VSCMD_ARG_HOST_ARCH -ne "x64" -or $Env:VSCMD_ARG_TGT_ARCH -ne "x64"){
    Write-Host "This script requires a x64 build environment." -ForegroundColor Red
    Write-Host "" 
    Write-Host "Try running it via 'x64 Native Tools Command Prompt', or from a x64 Developer Powershell Terminal."
    Write-Host ""
    Write-Host "Note that the default Powershell Terminal within Visual Studio 2019 will not work, as it is x86 instead of x64. You must choose x64 either via the Start Menu, or follow the instructions from here to get a x64 Powershell Terminal within Visual Studio: <https://developercommunity.visualstudio.com/idea/943058/x64-developer-powershell-for-vs-2019.html>"
    Exit 1
}

if ( $args.Count -ne 2 ){
    Write-Host "Usage: hot-reload source_app_folder build_folder"
    Exit 1
}

$global:PathToMonitor = Convert-Path $args[0]
$global:Buildpath = Convert-Path $args[1]

if ((Test-Path -Path "$Buildpath\build.ninja") -ne $true){
    Write-Host "$Buildpath\build.ninja not found." -ForegroundColor Red
    Write-Host "You must provide a valid for the build folder. This is typically the folder in which the .exe of our app lives."
    Write-Host $args.Count
    Exit 1
}


# make sure you adjust this to point to the folder you want to monitor
# $global:PathToMonitor = "C:\Users\tim\Documents\dev\island\apps\examples\hello_triangle\hello_triangle_app"
# This is the folder which contains the build.ninja file that should be triggered upon change detected.
# $global:Buildpath = "C:\Users\tim\Documents\dev\island\apps\examples\hello_triangle\out\build\x64-Debug"

# explorer $PathToMonitor

$FileSystemWatcher = New-Object System.IO.FileSystemWatcher
$FileSystemWatcher.Path  = $PathToMonitor
$FileSystemWatcher.IncludeSubdirectories = $true

# make sure the watcher emits events
$FileSystemWatcher.EnableRaisingEvents = $true

# define the code that should execute when a file change is detected
$Action = {
    $details = $event.SourceEventArgs
    $Name = $details.Name
    $FullPath = $details.FullPath
    $OldFullPath = $details.OldFullPath
    $OldName = $details.OldName
    $ChangeType = $details.ChangeType
    $Timestamp = $event.TimeGenerated
    
    # Execute code based on change type
    switch ($ChangeType)
    {
        'Changed' { "CHANGE" 
             If($Name -match '\.cpp$' -Or $Name -match '\.h$'){
                $text = "File {0} was changed" -f $Name
                Write-Host ""
                Write-Host $text -ForegroundColor Yellow
                Write-Host (ninja -C $Buildpath )
                Write-Host "Done." -ForegroundColor Yellow
                $global:cursor_pos = $host.UI.RawUI.CursorPosition
            }
        }
        'Created' { "CREATED"}
        'Deleted' { "DELETED"}
        'Renamed' { 
            If($Name -match '\.cpp$' -Or $Name -match '\.h$'){
                $text = "File {0} was changed" -f  $Name
                Write-Host $text -ForegroundColor Yellow
                Write-Host (ninja -C $Buildpath )
                Write-Host "Done." -ForegroundColor Yellow
                $global:cursor_pos = $host.UI.RawUI.CursorPosition
            }
        }
        default { 
         Write-Host $_ -ForegroundColor Red -BackgroundColor White 
         $global:cursor_pos = $host.UI.RawUI.CursorPosition
        }
    }
}

# add event handlers
$handlers = . {
    Register-ObjectEvent -InputObject $FileSystemWatcher -EventName Changed -Action $Action -SourceIdentifier FSChange
    # Register-ObjectEvent -InputObject $FileSystemWatcher -EventName Created -Action $Action -SourceIdentifier FSCreate
    # Register-ObjectEvent -InputObject $FileSystemWatcher -EventName Deleted -Action $Action -SourceIdentifier FSDelete
    Register-ObjectEvent -InputObject $FileSystemWatcher -EventName Renamed -Action $Action -SourceIdentifier FSRename
}

Write-Host "Starting hot-reloading watcher. Ctrl+C to quit." -ForegroundColor Green
Write-Host "Buildpath: $Buildpath"
Write-Host "Watching for changes in '.cpp' and '.h' files in $PathToMonitor"

$host.UI.RawUI.CursorSize = 0

$global:cursor_pos = $host.UI.RawUI.CursorPosition

try
{
    $progress_indicator = "/-\|/-\|"
    $idx = 0
    do
    {
        Wait-Event -Timeout 1
        $global:old_cursor = $host.UI.RawUI.CursorPosition
        $host.UI.RawUI.CursorPosition = New-Object System.Management.Automation.Host.Coordinates ($global:cursor_pos.X+1), $global:cursor_pos.Y
        Write-Host $progress_indicator[$idx] -NoNewline -ForegroundColor Blue
        $host.UI.RawUI.CursorPosition = $global:old_cursor
        $idx++
        $idx %= 8
        
    } while ($true)
}
finally
{
    # this gets executed when user presses CTRL+C
    # remove the event handlers
    Unregister-Event -SourceIdentifier FSChange
    # Unregister-Event -SourceIdentifier FSCreate
    # Unregister-Event -SourceIdentifier FSDelete
    Unregister-Event -SourceIdentifier FSRename
    # remove background jobs
    $handlers | Remove-Job
    # remove filesystemwatcher
    $FileSystemWatcher.EnableRaisingEvents = $false
    $FileSystemWatcher.Dispose()
    Write-Host "Event Handler disabled." -ForegroundColor Green
    $host.UI.RawUI.CursorSize = 100
}
