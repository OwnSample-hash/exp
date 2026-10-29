from mysmb import MYSMB
from colors import c
from impacket import smb, smbconnection, nt_errors
from impacket.uuid import uuidtup_to_bin
from impacket.dcerpc.v5.rpcrt import DCERPCException
from struct import pack
import sys
import argparse


'''
Script for
- check target if MS17-010 is patched or not.
- find accessible named pipe
'''


def check_ms17_010(conn):
    TRANS_PEEK_NMPIPE = 0x23
    recvPkt = conn.send_trans(pack('<H', TRANS_PEEK_NMPIPE), maxParameterCount=0xffff, maxDataCount=0x800)
    status = recvPkt.getNTStatus()
    if status == 0xC0000205:  # STATUS_INSUFF_SERVER_RESOURCES
        print(c.f.GREEN + '[!] The target is not patched' + c.RESET)
    else:
        print(c.f.RED + '[-] The target is patched' + c.RESET)
        sys.exit()

def check_accessible_pipes(conn):
    print('=== Testing named pipes ===')
    conn.find_named_pipe(firstOnly=False)

def main(domain, username, password, remoteName, target_ip, port=445):
    #In case the password contains '@'
    if '@' in remoteName:
        password = password + '@' + remoteName.rpartition('@')[0]
        remoteName = remoteName.rpartition('@')[2]

    if domain is None:
        domain = ''

    if password == '' and username != '': # and options.hashes is None and options.no_pass is False and options.aesKey is None:
        from getpass import getpass
        password = getpass("Password:")

    if target_ip is None:
        target_ip = remoteName

    conn = MYSMB(target_ip, int(port))
    try:
        conn.login(username, password)
    except smb.SessionError as e:
        print(c.f.RED + '[-] Login failed: ' + nt_errors.ERROR_MESSAGES[e.error_code][0] + c.RESET)
        sys.exit()
    finally:
        print(c.f.BLUE + '[*] Target OS: ' + conn.get_server_os() + c.RESET)

    tid = conn.tree_connect_andx('\\\\'+target_ip+'\\'+'IPC$')
    conn.set_default_tid(tid)

    check_ms17_010(conn)
    check_accessible_pipes(conn)

    conn.disconnect_tree(tid)
    conn.logoff()
    conn.get_socket().close()

    print(c.f.CYAN +'[*] Done' + c.RESET)


