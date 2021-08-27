$global:ScriptPath = $PSScriptRoot
$global:IslandBasePath = Convert-Path "$ScriptPath\..\.."
$global:IslandAppsPath = Convert-Path "$IslandBasePath\apps"

# List the tests you want to perform inside the file tests.txt
# adjacent to this file.
$TestListPath = (Join-Path -Path $ScriptPath -ChildPath "tests.txt")

function Invoke-Build {

    param (
        [string]$SrcFolder,
        [string]$BuildType
    )

    Write-Host "Starting ${BuildType} build for ${SrcFolder}." -ForegroundColor Yellow

    $folder_name = "build_${BuildType}"

    Write-Host "folder name: ${folder_name}"

    cd ${SrcFolder}
    mkdir $folder_name 
    cd $folder_name

    # pwd

    cmake -G Ninja -DCC="cl" -DCXX="cl" -DCMAKE_BUILD_TYPE="${BuildType}" ..
    ninja

    if ($? -ne $True) { 
        Write-Host "ERROR: Build ${BuildType} failed in ${SrcFolder}." -ForegroundColor Red 
        exit 1  
    }

    Write-Host "Completed ${BuildType} build for ${SrcFolder}." -ForegroundColor Green

}

# Read file with list of tests

$stream_reader = New-Object System.IO.StreamReader($TestListPath) 
while (($current_line =$stream_reader.ReadLine()) -ne $null)
{

    # Lines look typically like this:
    #   examples/hello_world:Island-HelloWorld
    # so we're splitting, and keeping only the bit before the first colon (":")
    $TestPath = ($current_line -Split ":")[0]

    Invoke-Build -SrcFolder (Join-Path $IslandAppsPath $TestPath) -BuildType "Debug"
    Invoke-Build -SrcFolder (Join-Path $IslandAppsPath $TestPath) -BuildType "Release"
    
}
