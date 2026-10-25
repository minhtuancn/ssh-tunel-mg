@echo off
setlocal

set "psfile=%temp%\ssh_tunnel_gui.ps1"

(
echo Add-Type -AssemblyName System.Windows.Forms
echo Add-Type -AssemblyName System.Drawing
echo.
echo $Form = New-Object System.Windows.Forms.Form
echo $Form.Text = "SSH Tunnel Manager"
echo $Form.Size = New-Object System.Drawing.Size^(500,420^)
echo $Form.StartPosition = "CenterScreen"
echo $Form.FormBorderStyle = "FixedDialog"
echo $Form.MaximizeBox = $false
echo $Form.Font = New-Object System.Drawing.Font^("Segoe UI",10^)
echo.
echo # User
echo $lbl1 = New-Object System.Windows.Forms.Label
echo $lbl1.Text = "User"
echo $lbl1.Size = "50,25"
echo $lbl1.Location = "20,20"
echo $Form.Controls.Add^($lbl1^)
echo.
echo $cmbUser = New-Object System.Windows.Forms.ComboBox
echo $cmbUser.Location = "80,18"
echo $cmbUser.Size = "300,30"
echo $cmbUser.Items.AddRange^(@("dev","ubuntu","root"^)^)
echo $cmbUser.SelectedIndex = 0
echo $Form.Controls.Add^($cmbUser^)
echo.
echo # IP
echo $lbl2 = New-Object System.Windows.Forms.Label
echo $lbl2.Text = "IP"
echo $lbl2.Size = "50,25"
echo $lbl2.Location = "20,65"
echo $Form.Controls.Add^($lbl2^)
echo.
echo $cmbIPRange = New-Object System.Windows.Forms.ComboBox
echo $cmbIPRange.Location = "80,63"
echo $cmbIPRange.DropDownStyle = "DropDownList"
echo $cmbIPRange.Size = "160,30"
echo $cmbIPRange.Items.AddRange^(@("10.20.10.","192.168.1.","10.0.0.","172.16.0."^)^)
echo $cmbIPRange.SelectedIndex = 0
echo $Form.Controls.Add^($cmbIPRange^)
echo.
echo $txtIPLast = New-Object System.Windows.Forms.TextBox
echo $txtIPLast.Location = "250,63"
echo $txtIPLast.Size = "130,30"
echo $txtIPLast.Text = "103"
echo $Form.Controls.Add^($txtIPLast^)
echo.
echo # Port
echo $lbl4 = New-Object System.Windows.Forms.Label
echo $lbl4.Text = "Port"
echo $lbl4.Size = "50,25"
echo $lbl4.Location = "20,110"
echo $Form.Controls.Add^($lbl4^)
echo.
echo $txtPort = New-Object System.Windows.Forms.TextBox
echo $txtPort.Location = "80,108"
echo $txtPort.Size = "300,30"
echo $txtPort.Text = "3000"
echo $Form.Controls.Add^($txtPort^)
echo.
echo # Buttons
echo $btnConnect = New-Object System.Windows.Forms.Button
echo $btnConnect.Text = "Connect"
echo $btnConnect.Location = "50,160"
echo $btnConnect.Size = "120,40"
echo $Form.Controls.Add^($btnConnect^)
echo.
echo $btnBrowser = New-Object System.Windows.Forms.Button
echo $btnBrowser.Text = "Open Browser"
echo $btnBrowser.Location = "190,160"
echo $btnBrowser.Size = "120,40"
echo $Form.Controls.Add^($btnBrowser^)
echo.
echo $btnDisconnect = New-Object System.Windows.Forms.Button
echo $btnDisconnect.Text = "Disconnect All"
echo $btnDisconnect.Location = "330,160"
echo $btnDisconnect.Size = "120,40"
echo $Form.Controls.Add^($btnDisconnect^)
echo.
echo # Active forwards list
echo $lbl5 = New-Object System.Windows.Forms.Label
echo $lbl5.Text = "Active Forwards:"
echo $lbl5.Location = "20,225"
echo $Form.Controls.Add^($lbl5^)
echo.
echo $lstForward = New-Object System.Windows.Forms.ListBox
echo $lstForward.Location = "20,245"
echo $lstForward.Size = "460,85"
echo $Form.Controls.Add^($lstForward^)
echo.
echo # Status
echo $Status = New-Object System.Windows.Forms.Label
echo $Status.Location = "20,355"
echo $Status.Size = "460,30"
echo $Status.Text = "Ready"
echo $Form.Controls.Add^($Status^)
echo.
echo $script:tunnelPids = New-Object System.Collections.ArrayList
echo.
echo # Functions
echo function Refresh-Forwards {
echo     $lstForward.Items.Clear^(^)
echo     $script:tunnelPids.Clear^(^)
echo     try {
echo         $procs = Get-CimInstance Win32_Process -Filter "Name='ssh.exe'" ^| Where-Object { $_.CommandLine -like '*-L*' }
echo         foreach ($p in $procs^) {
echo             $cmd = $p.CommandLine
echo             if ($cmd -match '-L\s+(\d+^):'^) {
echo                 $port = $matches[1]
echo                 $user = if ($cmd -match '(\w+^)@'^) { $matches[1] } else { "?" }
echo                 $ip = if ($cmd -match '@([\d.]+^)'^) { $matches[1] } else { "?" }
echo                 [void]$script:tunnelPids.Add^($p.ProcessId^)
echo                 [void]$lstForward.Items.Add^("Port $port  $user@$ip"^)
echo             }
echo         }
echo         if ($lstForward.Items.Count -eq 0^) {
echo             [void]$lstForward.Items.Add^("(No active tunnels)"^)
echo         }
echo     } catch {
echo         [void]$lstForward.Items.Add^("(Unable to query processes)"^)
echo     }
echo }
echo.
echo # Double-click on item to kill specific tunnel
echo $lstForward.Add_DoubleClick^({
echo     $idx = $lstForward.SelectedIndex
echo     if ($idx -ge 0 -and $idx -lt $script:tunnelPids.Count^) {
echo         $pid = $script:tunnelPids[$idx]
echo         if ($pid -gt 0^) {
echo             $proc = Get-Process -Id $pid -ErrorAction SilentlyContinue
echo             if ($proc^) { $proc ^| Stop-Process -Force }
echo         }
echo     }
echo     Refresh-Forwards
echo }^)
echo.
echo # Connect
echo $btnConnect.Add_Click^({
echo     $user = $cmbUser.Text
echo     $ip = $cmbIPRange.Text + $txtIPLast.Text
echo     $port = $txtPort.Text
echo     if ^(-not $port^) {
echo         [System.Windows.Forms.MessageBox]::Show^("Port khong hop le"^)
echo         return
echo     }
echo     $Status.Text = "Connecting..."
echo     $p = Start-Process ssh -WindowStyle Hidden -PassThru -ArgumentList "-N -L ${port}:127.0.0.1:${port} $user@$ip"
echo     if ($p^) { $script:tunnelPids.Add^($p.Id^) }
echo     $Status.Text = "Connected: $user@$ip  port $port"
echo     Refresh-Forwards
echo }^)
echo.
echo # Open Browser
echo $btnBrowser.Add_Click^({
echo     Start-Process "http://localhost:$($txtPort.Text)"
echo }^)
echo.
echo # Disconnect
echo $btnDisconnect.Add_Click^({
echo     foreach ($pid in $script:tunnelPids^) {
echo         if ($pid -gt 0^) {
echo             $proc = Get-Process -Id $pid -ErrorAction SilentlyContinue
echo             if ($proc^) { $proc ^| Stop-Process -Force }
echo         }
echo     }
echo     $script:tunnelPids.Clear^(^)
echo     $Status.Text = "Disconnected"
echo     Refresh-Forwards
echo }^)
echo.
echo # Load
echo $Form.Add_Shown^({
echo     Refresh-Forwards
echo }^)
echo.
echo [void]$Form.ShowDialog^(^)
) > "%psfile%"

powershell -WindowStyle Hidden -ExecutionPolicy Bypass -NoProfile -File "%psfile%"

del "%psfile%" >nul 2>&1
endlocal
