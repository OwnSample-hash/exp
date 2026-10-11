from .eb import *


def RegisterPlugin():
    return {
        "name": "eb",
        "version": "0.1.0",
        "description": "EB plugin for explo",
        "tags": ["eb", "explo", "plugin"],
        "vars": {
            "int_": 1,
            "str_": "Hello, explo!",
        },
        "descs": {
            "int_": "An integer variable",
            "str_": "A string variable"
        },
        "rootRequired": False,
        "makeCommand": False,
        "shutdown": lambda: print("Shutting down EB plugin..."),
        "initialize": lambda: print("Initializing EB plugin..."),
        "execute": eb
    }