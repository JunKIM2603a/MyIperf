@echo off
setlocal enabledelayedexpansion

rem ==============================================================================
rem Multi-node Tester Batch Script for Windows (v11 - Loop Fix)
rem
rem This version fixes a fundamental bug where the main 'for' loop only ever
rem executed once. The 'goto' based wait loop has been moved into a
rem subroutine, which allows the main repetition loop to function correctly.
rem
rem Usage:
rem Multi-node_tester.bat <repetitions> <packet_size> <num_packets> <interval_ms>
rem
rem Options:
rem   <repetitions> : The number of times to repeat the full set of 5 server/5 client tests.
rem   <packet_size> : The size of each data packet in bytes.
rem   <num_packets> : The total number of packets to send per client.
rem   <interval_ms> : The interval in milliseconds between sending packets.
rem
rem Examples:
rem   Run 5 times, 1500 byte packets, 10000 packets, 10ms interval:
rem   Multi-node_tester.bat 5 1500 10000 10
rem ==============================================================================

rem --- Configuration ---
set "EXECUTABLE=.\build\Release\IPEFTC.exe"
set "LOG_DIR=.\Log"
set "SERVER_IP=0.0.0.0"
set "CLIENT_TARGET_IP=127.0.0.1"
set "PORTS=60000"
set "EXPECTED_PROCESS_COUNT=2"

rem --- Argument Validation ---
if "%~4"=="" (
    echo Error: Invalid number of arguments.
    echo Usage: %~n0 ^<repetitions^> ^<packet_size^> ^<num_packets^> ^<interval_ms^>
    exit /b 1
)

set "REPETITIONS=%1"
set "PACKET_SIZE=%2"
set "NUM_PACKETS=%3"
set "INTERVAL_MS=%4"

rem --- Count Existing Logs ---
echo Counting existing log files...
set "initial_log_count=0"
if exist "%LOG_DIR%\*.log" (
    for /f %%A in ('dir /b "%LOG_DIR%\*.log" ^| find /c /v ""') do set "initial_log_count=%%A"
)
echo Found %initial_log_count% existing log files to skip during summary.

rem --- Main Loop ---
for /L %%i in (1, 1, %REPETITIONS%) do (
    echo =================================================
    echo --- Starting Iteration %%i of %REPETITIONS% ---
    echo =================================================

    rem Start all servers in the background
    for %%p in (%PORTS%) do (
        echo [Iteration %%i] Starting server on port %%p...
        start /B "Server %%p" %EXECUTABLE% --mode server --target %SERVER_IP% --port %%p --save-logs true
    )

    echo Waiting for servers to initialize...
    timeout /t 3 /nobreak > nul

    rem Start all clients in the background
    for %%p in (%PORTS%) do (
        echo [Iteration %%i] Starting client for port %%p...
        start /B "Client %%p" %EXECUTABLE% --mode client --target %CLIENT_TARGET_IP% --port %%p --packet-size %PACKET_SIZE% --num-packets %NUM_PACKETS% --interval-ms %INTERVAL_MS% --save-logs true
    )
    
    echo All processes for iteration %%i have been launched.

    rem --- Verification and Wait Step ---
    call :VerifyAndWaitForProcesses %%i
    
    echo --- Iteration %%i finished ---
    timeout /t 1 /nobreak > nul
)

echo =================================================
echo All test iterations completed.
echo =================================================
echo.
echo Adding a 5-second delay for file system to settle...
timeout /t 5 /nobreak > nul
echo.

echo =================================================
echo ---      CONSOLIDATED FINAL TEST SUMMARY      ---
echo =================================================
echo.

rem --- Summarization Step ---
set "skip_count=%initial_log_count%"
echo Skipping first %skip_count% oldest files from summary.
echo.

rem Use /OD to sort by date/time, oldest first.
for /f "skip=%skip_count% delims=" %%F in ('dir /b /od "%LOG_DIR%\*.log"') do (
    set "log_file=%%F"
    echo -------------------------------------------------
    echo --- Summary from: !log_file!
    echo -------------------------------------------------
    
    set "printing=false"
    for /f "tokens=1* delims=:" %%A in ('type "%LOG_DIR%\!log_file!" 2^>nul ^| findstr /n "^"') do (
        set "line=%%B"
        if not defined line set "line= "

        if "!line:FINAL TEST SUMMARY=!" NEQ "!line!" (
            set "printing=true"
        )
        if "!printing!"=="true" (
            echo !line!
        )
        if "!line:=================================================!" NEQ "!line!" (
            if "!line:FINAL TEST SUMMARY=!" EQU "!line!" (
                set "printing=false"
            )
        )
    )
    echo.
)

echo =================================================
echo ---           END OF SUMMARY                ---
echo =================================================
echo.

goto :eof

rem ==============================================================================
:VerifyAndWaitForProcesses
rem Subroutine to verify process startup and wait for them to complete.
rem %1: The current iteration number (for display purposes)
rem ==============================================================================
echo Verifying process startup for iteration %1...
timeout /t 3 /nobreak > nul
set "process_count=0"
for /f %%a in ('tasklist /FI "IMAGENAME eq IPEFTC.exe" ^| find /c "IPEFTC.exe"') do set "process_count=%%a"

if %process_count% LSS %EXPECTED_PROCESS_COUNT% (
    echo WARNING: Expected %EXPECTED_PROCESS_COUNT% processes, but only found %process_count%.
    echo NOTE: This can happen if the test duration is very short and processes have already completed.
) else (
    echo Verification successful: %process_count% processes are running.
)

echo Waiting for all tests to complete for iteration %1...
:waitloop_sub
tasklist /FI "IMAGENAME eq IPEFTC.exe" 2>NUL | find /I /N "IPEFTC.exe" > NUL
if "%ERRORLEVEL%"=="0" (
    timeout /t 5 /nobreak > nul
    goto :waitloop_sub
)
exit /b
rem ==============================================================================