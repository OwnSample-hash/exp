#!/usr/bin/env python3
import os
import json
import pathlib
import argparse
import argcomplete
import tabulate


class FileTemplate:
    name: str
    fargs: list[str]
    skip_first_line: bool
    template_file: str

    def __init__(
        self,
        name: str | function,
        fargs: list[str] | function,
        skip_first_line: bool,
        template_file: str | function,
    ):
        global config_data
        if callable(name):
            self.name = name(config_data)
        elif isinstance(name, str):
            self.name = name
        else:
            raise TypeError("name must be a string or a callable returning a string")
        if callable(fargs):
            self.fargs = fargs(self.name, config_data)
        elif isinstance(fargs, list):
            self.fargs = fargs
        else:
            raise TypeError("fargs must be a list or a callable returning a list")
        self.skip_first_line = skip_first_line
        if callable(template_file):
            self.template_file = template_file(config_data)
        elif isinstance(template_file, str):
            self.template_file = template_file
        else:
            raise TypeError(
                "template_file must be a string or a callable returning a string"
            )
        self.fargs = self.lookup_fargs(config_data)

    def lookup_fargs(self, config_data: dict) -> list[str]:
        resolved_fargs = []
        for arg in self.fargs:
            try:
                resolved_fargs.append(config_data[arg])
            except KeyError:
                resolved_fargs.append(arg)
        return resolved_fargs


templates: list[FileTemplate] = []


def load_fargs(name: str, config_data: dict) -> list[str]:
    raw_list = []
    template_path = pathlib.Path(
        os.getcwd(),
        "templates",
        name.split("/")[0],
        f"vargs_{config_data["CONFIG_NEW_MODULE_TYPE"]}.txt",
    )
    with open(template_path, "r") as f:
        raw_list = f.readlines()
    return [x.replace(",", "").strip() for x in raw_list]


def to_bool(_:str, config_data: dict) -> list[str]:
    return ["true" if config_data["CONFIG_NEW_MODULE_ENABLED"] else "false"]


def generate_templates() -> list[FileTemplate]:
    return [
        FileTemplate(
            name="CMakeLists.txt",
            fargs=[
                "CONFIG_NEW_MODULE_UP_NAME",
                "CONFIG_NEW_MODULE_NAME",
                "CONFIG_NEW_MODULE_NAME",
            ],
            skip_first_line=False,
            template_file="templates/CMakeLists.txt",
        ),
        FileTemplate(
            name="tests/CMakeLists.txt",
            fargs=[
                "CONFIG_NEW_MODULE_NAME",
                "CONFIG_NEW_MODULE_NAME",
                "CONFIG_NEW_MODULE_UP_NAME",
                "CONFIG_NEW_MODULE_UP_NAME",
                "CONFIG_NEW_MODULE_UP_NAME",
                "CONFIG_NEW_MODULE_UP_NAME",
            ],
            skip_first_line=False,
            template_file="templates/tests/CMakeLists.txt",
        ),
        FileTemplate(
            name=(
                lambda x: f"tests/test_{x["CONFIG_NEW_MODULE_NAME"]}.cpp"  # pyright: ignore
            ),
            fargs=[
                "CONFIG_NEW_MODULE_NAME",
                "CONFIG_NEW_MODULE_UP_NAME",
                "CONFIG_NEW_MODULE_NAME",
                "CONFIG_NEW_MODULE_NAME",
                "CONFIG_NEW_MODULE_UP_NAME",
                "CONFIG_NEW_MODULE_NAME",
                "CONFIG_NEW_MODULE_NAME",
            ],
            skip_first_line=True,
            template_file="templates/tests/test.cpp",
        ),
        FileTemplate(
            name=(
                lambda x: f"src/{x["CONFIG_NEW_MODULE_NAME"]}.cpp"  # pyright: ignore
            ),
            fargs=load_fargs,  # pyright: ignore
            skip_first_line=True,
            template_file=(
                lambda x: f"templates/src/template_{x["CONFIG_NEW_MODULE_TYPE"]}.cpp"
            ),  # pyright: ignore
        ),
        FileTemplate(
            name=(
                lambda x: f"include/{x["CONFIG_NEW_MODULE_NAME"]}.hpp"  # pyright: ignore
            ),
            fargs=load_fargs,  # pyright: ignore
            skip_first_line=True,
            template_file=(
                lambda x: f"templates/include/template_{x["CONFIG_NEW_MODULE_TYPE"]}.hpp"
            ),  # pyright: ignore
        ),
        FileTemplate(
            name="config.yaml",
            fargs=to_bool, # pyright: ignore
            skip_first_line=False,
            template_file="templates/config.yaml",
        ),
    ]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate files from templates.")
    parser.add_argument(
        "--config",
        "-c",
        type=str,
        help="Path to the JSON configuration file.",
        default="/tmp/new_mod.json",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Perform a dry run without creating files.",
    )
    parser.add_argument(
        "--dry-run-full",
        action="store_true",
        help="Outputs the formatted content of each generated file to stdout.",
    )
    argcomplete.autocomplete(parser)
    args = parser.parse_args()

    try:
        with open(args.config, "r") as f:
            global config_data
            config_data = json.load(f)
            config_data["CONFIG_NEW_MODULE_UP_NAME"] = config_data[
                "CONFIG_NEW_MODULE_NAME"
            ].upper()
    except Exception as e:
        print(f"Error reading config file: {e}")
        exit(1)

    if os.path.exists(
        pathlib.Path(os.getcwd(), "plugins", config_data["CONFIG_NEW_MODULE_NAME"])
    ):
        print(f"Module {config_data['CONFIG_NEW_MODULE_NAME']} already exists.")
        exit(1)
    else:
        os.makedirs(
            pathlib.Path(os.getcwd(), "plugins", config_data["CONFIG_NEW_MODULE_NAME"])
        )

    templates = generate_templates()

    if args.dry_run:
        table_data = []
        for template in templates:
            target_path = pathlib.Path(
                os.getcwd(),
                "plugins",
                config_data["CONFIG_NEW_MODULE_NAME"],
                template.name,
            )
            table_data.append(
                [
                    str(target_path),
                    "Yes" if template.skip_first_line else "No",
                    template.template_file,
                    len(template.fargs),
                ]
            )
        print(
            tabulate.tabulate(
                table_data,
                headers=[
                    "Target Path",
                    "Skip First Line",
                    "Template File",
                    "Argument Count",
                ],
                tablefmt="grid",
            )
        )
        if args.dry_run_full:
            for template in templates:
                print(f"\nGenerating file from template: {template.template_file}")
                for args in template.fargs:
                    print(f"  Argument: {args}")
                target_path = pathlib.Path(
                    os.getcwd(),
                    "plugins",
                    config_data["CONFIG_NEW_MODULE_NAME"],
                    template.name,
                )
                with open(template.template_file, "r") as tf:
                    template_lines = tf.readlines()
                if template.skip_first_line:
                    template_lines = template_lines[1:]
                print(f"--- {target_path} ---")
                try:
                    content = "".join(template_lines) % tuple(template.fargs)
                except Exception as e:
                    print(f"Error formatting template {template.template_file}: {e}")
                    content = "".join(template_lines)
                print(content)
                print(f"--- End of {target_path} ---\n")
    else:
        for template in templates:
            target_path = pathlib.Path(
                os.getcwd(),
                "plugins",
                config_data["CONFIG_NEW_MODULE_NAME"],
                template.name,
            )
            print(f"Creating file: {target_path}")
            os.makedirs(target_path.parent, exist_ok=True)
            with open(template.template_file, "r") as tf:
                template_lines = tf.readlines()
            if template.skip_first_line:
                template_lines = template_lines[1:]
            content = "".join(template_lines) % tuple(template.fargs)
            with open(target_path, "w") as out_file:
                out_file.write(content)
        print(f"Module {config_data['CONFIG_NEW_MODULE_NAME']} created successfully.")
# Vim: set expandtab tabstop=4 shiftwidth=4:
