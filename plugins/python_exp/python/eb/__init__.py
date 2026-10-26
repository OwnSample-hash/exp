import explo
from .eb import *


def RegisterTool():
    return {
        "name": "eb",
        "version": "0.1.0",
        "description": "EB plugin for explo",
        "uuid": "123e4567-e89b-12d3-a456-426614174000",
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
        "shutdown": lambda: explo.info("Shutting down EB plugin..."),
        "initialize": lambda: explo.info("Initializing EB plugin..."),
        "execute": eb
    }