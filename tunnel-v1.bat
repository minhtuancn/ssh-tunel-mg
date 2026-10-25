@echo off
setlocal

set "psfile=%temp%\ssh_tunnel_gui.ps1"

(
echo Add-Type -AssemblyName System.Windows.Forms
echo Add-Type -AssemblyName System.Drawing
echo.
echo $Form = New-Object System.Windows.Forms.Form
echo $Form.Text = "SSH Tunnel Manager"
echo $Form.Size = New-Object System.Drawing.Size^(500,350^)
echo $Form.StartPosition = "CenterScreen"
echo $Form.FormBorderStyle = "FixedDialog"
echo $Form.MaximizeBox = $false
echo $Form.Font = New-Object System.Drawing.Font^("Segoe UI",10^)
echo.
echo $lbl1 = New-Object System.Windows.Forms.Label
echo $lbl1.Text = "User"
echo $lbl1.Location = "20,20"
echo $Form.Controls.Add^($lbl1^)
echo.
echo $cmbUser = New-Object System.Windows.Forms.ComboBox
echo $cmbUser.Location = "150,18"
echo $cmbUser.Size = "250,30"
echo $cmbUser.Items.AddRange^(@("dev","ubuntu","root"^)^)
echo $cmbUser.SelectedIndex = 0
echo $Form.Controls.Add^($cmbUser^)
echo.
echo $lbl2 = New-Object System.Windows.Forms.Label
echo $lbl2.Text = "IP Cuoi"
echo $lbl2.Location = "20,60"
echo $Form.Controls.Add^($lbl2^)
echo.
echo $txtIP = New-Object System.Windows.Forms.TextBox
echo $txtIP.Location = "150,58"
echo $txtIP.Size = "100,30"
echo $txtIP.Text = "103"
echo $Form.Controls.Add^($txtIP^)
echo.
echo $lbl3 = New-Object System.Windows.Forms.Label
echo $lbl3.Text = "Local Port"
echo $lbl3.Location = "20,100"
echo $Form.Controls.Add^($lbl3^)
echo.
echo $txtLocal = New-Object System.Windows.Forms.TextBox
echo $txtLocal.Location = "150,98"
echo $txtLocal.Size = "120,30"
echo $txtLocal.Text = "8080"
echo $Form.Controls.Add^($txtLocal^)
echo.
echo $lbl4 = New-Object System.Windows.Forms.Label
echo $lbl4.Text = "Remote Port"
echo $lbl4.Location = "20,140"
echo $Form.Controls.Add^($lbl4^)
echo.
echo $txtRemote = New-Object System.Windows.Forms.TextBox
echo $txtRemote.Location = "150,138"
echo $txtRemote.Size = "120,30"
echo $txtRemote.Text = "8080"
echo $Form.Controls.Add^($txtRemote^)
echo.
echo $btn1 = New-Object System.Windows.Forms.Button
echo $btn1.Text = "Connect"
echo $btn1.Location = "50,220"
echo $btn1.Size = "120,40"
echo $Form.Controls.Add^($btn1^)
echo.
echo $btn2 = New-Object System.Windows.Forms.Button
echo $btn2.Text = "Open Browser"
echo $btn2.Location = "190,220"
echo $btn2.Size = "120,40"
echo $Form.Controls.Add^($btn2^)
echo.
echo $btn3 = New-Object System.Windows.Forms.Button
echo $btn3.Text = "Disconnect All"
echo $btn3.Location = "330,220"
echo $btn3.Size = "120,40"
echo $Form.Controls.Add^($btn3^)
echo.
echo $Status = New-Object System.Windows.Forms.Label
echo $Status.Location = "20,280"
echo $Status.Size = "430,30"
echo $Status.Text = "Ready"
echo $Form.Controls.Add^($Status^)
echo.
echo $btn1.Add_Click^({
echo     $user = $cmbUser.Text
echo     $ip = "10.20.10." + $txtIP.Text
echo     $lp = $txtLocal.Text
echo     $rp = $txtRemote.Text
echo.
echo     if ^(-not $lp -or -not $rp^) {
echo         [System.Windows.Forms.MessageBox]::Show^("Port khong hop le"^)
echo         return
echo     }
echo.
echo     $Status.Text = "Connecting..."
echo.
echo     Start-Process ssh -WindowStyle Hidden -ArgumentList "-N -L $lp`:127.0.0.1`:$rp $user@$ip"
echo.
echo     $Status.Text = "Connected: $user@$ip  localhost:$lp"
echo }^)
echo.
echo $btn2.Add_Click^({
echo     Start-Process "http://localhost:$($txtLocal.Text)"
echo }^)
echo.
echo $btn3.Add_Click^({
echo     Get-Process ssh -ErrorAction SilentlyContinue ^| Stop-Process -Force
echo     $Status.Text = "Disconnected"
echo }^)
echo.
echo [void]$Form.ShowDialog^(^)
) > "%psfile%"

powershell -WindowStyle Hidden -ExecutionPolicy Bypass -NoProfile -File "%psfile%"

del "%psfile%" >nul 2>&1
endlocal
