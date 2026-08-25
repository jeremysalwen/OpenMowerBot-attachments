import arcpy
import paramiko
from scp import SCPClient

def PutFile(filename, host, username, password, path):
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    arcpy.AddMessage(username + password)
    ssh.connect(host, username=username, password=password)
    with SCPClient(ssh.get_transport()) as scp:
        scp.put(filename, remote_path=path) # Copy my_file.txt to the server
    scp.close()
    return

# This is used to execute code if the file was run but not imported
if __name__ == '__main__':

    # Tool parameter accessed with GetParameter or GetParameterAsText
    filename = arcpy.GetParameterAsText(0)
    host = arcpy.GetParameterAsText(1)  
    username = arcpy.GetParameterAsText(2)
    password = arcpy.GetParameterAsText(3)
    path =   arcpy.GetParameterAsText(4)
    
    PutFile(filename, host, username, password, path)
    
    # Update derived parameter values using arcpy.SetParameter() or arcpy.SetParameterAsText()