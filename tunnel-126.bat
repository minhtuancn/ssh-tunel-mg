@echo off

set /p LOCAL_PORT=Nhap port local:
set /p REMOTE_PORT=Nhap port remote:

ssh -N -L %LOCAL_PORT%:127.0.0.1:%REMOTE_PORT% openclaw@10.20.10.126