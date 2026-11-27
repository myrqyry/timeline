[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo',
    [ValidateSet('obs31', 'obs32', 'obs33')]
    [string] $ObsCompatibility = 'obs32'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Build-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "A 64-bit system is required to build the project."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The obs-studio PowerShell build script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Build {
    trap {
        Pop-Location -Stack BuildTemp -ErrorAction 'SilentlyContinue'
        Write-Error $_
        Log-Group
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach($Utility in $UtilityFunctions) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    Push-Location -Stack BuildTemp
    Ensure-Location $ProjectRoot

    $Preset = "windows-${Target}"
    $ConfigurePreset = "windows-ci-${Target}"
    $BuildDirectory = "build_${Target}"
    $PluginDestination = 'timeline/bin/64bit'
    if ($ObsCompatibility -eq 'obs33') {
        $Preset = "windows-obs33-${Target}"
        $ConfigurePreset = "windows-ci-obs33-${Target}"
        $BuildDirectory = "build_obs33_${Target}"
        $PluginDestination = 'timeline'
    }

    $CmakeArgs = @('--fresh', '--preset', $ConfigurePreset, "-DTIMELINE_OBS_COMPATIBILITY=${ObsCompatibility}", '-DTIMELINE_BUILD_TESTS=OFF', "-DOBS_PLUGIN_DESTINATION=${PluginDestination}", '-DOBS_DATA_DESTINATION=timeline/data')
    if ($ObsCompatibility -eq 'obs33' -and $Configuration -ne 'Debug') {
        $CmakeArgs += @('-DTIMELINE_OBS_SDK_RELEASE_ONLY=ON', '-DCMAKE_CONFIGURATION_TYPES=Release;RelWithDebInfo;MinSizeRel')
    }
    $CmakeBuildArgs = @('--build')
    $CmakeInstallArgs = @()

    if ( $DebugPreference -eq 'Continue' ) {
        $CmakeArgs += ('--debug-output')
        $CmakeBuildArgs += ('--verbose')
        $CmakeInstallArgs += ('--verbose')
    }

    $CmakeBuildArgs += @(
        '--preset', $Preset
        '--config', $Configuration
        '--parallel'
        '--', '/consoleLoggerParameters:Summary', '/noLogo'
    )

    $CmakeInstallArgs += @(
        '--install', $BuildDirectory
        '--prefix', "${ProjectRoot}/release/${ObsCompatibility}/${Configuration}"
        '--config', $Configuration
    )

    Log-Group "Configuring ${ProductName}..."
    Invoke-External cmake @CmakeArgs

    Log-Group "Building ${ProductName}..."
    Invoke-External cmake @CmakeBuildArgs

    Log-Group "Installing ${ProductName}..."
    Invoke-External cmake @CmakeInstallArgs

    Pop-Location -Stack BuildTemp
    Log-Group
}

Build
