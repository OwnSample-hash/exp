import explo
from .eternal_checker import *

def RegisterTool():
    return {
        "name": "check_eb",
        "version": "0.1.0",
        "description": "Check if target is vulnerable to EB",
        "uuid": "ce1b2f84-20a9-4b2c-81c3-b79fcf36a034",
        "tags": ["eb", "check"],
        "vars": {
            "domain": "",
            "username": "",
            "password": "",
            "remoteName": "",
            "target": "",
            "port": 445,
        },
        "descs": {
            "domain": "Domain name (default: empty)",
            "username": "Username (default: empty)",
            "password": "Password (default: empty)",
            "remoteName": "Remote name (default: empty)",
            "target": "Target IP address can be used instead of remoteName (default: None)",
            "port": "Target port (default: 445)"
        },
        "rootRequired": False,
        "makeCommand": True,
        "shutdown": lambda: explo.info("Shutting down Check EB plugin..."),
        "initialize": lambda: explo.info("Initializing Check EB plugin..."),
        "execute": lambda: main(
            explo.var("domain"),
            explo.var("username"),
            explo.var("password"),
            explo.var("remoteName"),
            explo.var("target"),
            explo.var("port")
        )
    }
# Vim: set expandtab tabstop=4 shiftwidth=4:
