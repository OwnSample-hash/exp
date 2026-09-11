from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from conf import *


def format_txt(txt: str, opt: ConfigOption, *args, **kwargs) -> str:
    for word in txt.split():
        if word.startswith("$") and word.endswith("$"):
            key = word[1:-1]
            if hasattr(opt, key):
                value = getattr(opt, key)
                txt = txt.replace(word, str(value))
    return txt.format(*args, **kwargs)


def generate_func(opts: list[ConfigOption], f: Any = None, depth: int = 0, prefix: str = "") -> list[str]:
    if depth == 5:
        logger.warning("Maximum depth reached, skipping further generation")
        return []
    if not f:
        f = open("cmake/Options.cmake", "w")
    for opt in opts:
        if opt.type == ConfigType.MENU or opt.type == ConfigType.DYNAMICMENU:
            if depth == 0:
                generate_func(opt.children, f, depth + 1, "")
            else:
                generate_func(opt.children, f, depth + 1, prefix + opt.name.upper() + "_")
        if not opt.cmake_export:
            continue
        plugin_name = os.path.dirname(opt.source_file).split(os.sep)[-1].upper()
        if opt.type == ConfigType.BOOL:
            f.write(
                f"option({prefix}{opt.name.upper()} \"{format_txt(opt.cmake_help if opt.cmake_help else '', opt, plugin_name=plugin_name)}\" {"ON" if opt.value else "OFF"})\n"
            )
        elif (
            opt.type == ConfigType.STRING
            or opt.type == ConfigType.INT
            or opt.type == ConfigType.CHOICE
        ):
            f.write(
                f"set(CACHE{{{prefix}{opt.name.upper()}}} TYPE STRING HELP \"{format_txt(opt.cmake_help if opt.cmake_help else '', opt, plugin_name=plugin_name)}\" VALUE \"{opt.value}\")\n"
            )
    if depth == 0:
        f.close()
        return ["cmake/Options.cmake"]
    else:
        return []


@register_generator
def gen():
    return Generator(
        name="Generate Cmake file for plugin",
        description="Generate Cmake file for plugin",
        gen=generate_func,
    )


# Vim: set expandtab tabstop=4 shiftwidth=4:
