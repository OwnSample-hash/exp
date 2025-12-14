#!/usr/bin/env python3
"""
Menuconfig-style Configuration System
A terminal-based configuration menu similar to Linux kernel menuconfig
"""

import argparse
import curses
import logging
import json
import os
import sys
import time as time2
import yaml
from dataclasses import dataclass, field
from datetime import date, time, timedelta
from enum import Enum
from glob import glob
from typing import List, Optional, Any
from pathlib import Path


class ConfigType(Enum):
    BOOL = "bool"
    TRISTATE = "tristate"
    STRING = "string"
    INT = "int"
    CHOICE = "choice"
    MENU = "menu"
    DYNAMICMENU = "dynamicmenu"


@dataclass
class ConfigOption:
    name: str
    prompt: str
    type: ConfigType
    default: Any = None
    help_text: str = ""
    help_text_fmt: Optional[str] = ""
    choices: list[dict[str, str]] = field(default_factory=list)
    children: list["ConfigOption"] = field(default_factory=list)
    range: Optional[dict[str, int]] = None
    depends_on: Optional[List[str]] = None
    show_if: Optional[list[str]] = None
    value: Any = None
    source_file: str = ""

    def __post_init__(self):
        if self.value is None:
            self.value = self.default


def error_popup(stdscr: curses.window, message: str):
    """Display an error popup"""
    logger.verbose_1("Displaying error popup")  # pyright: ignore
    logger.verbose_2(f"{message=}")  # pyright: ignore
    height, width = stdscr.getmaxyx()
    popup_height = 5
    popup_width = len(message) + 4
    popup_y = (height - popup_height) // 2
    popup_x = (width - popup_width) // 2

    popup_win = stdscr.subwin(popup_height, popup_width, popup_y, popup_x)
    popup_win.clear()
    popup_win.attron(curses.color_pair(4))
    popup_win.bkgd(" ", curses.color_pair(4))
    popup_win.border()
    popup_win.addstr(2, 2, message)
    popup_win.attroff(curses.color_pair(4))
    popup_win.refresh()
    stdscr.getch()
    stdscr.clear()
    stdscr.refresh()


def question_popup(
    stdscr: curses.window, header: str, footer: str, body_prefix: str = "", *body: str
) -> bool:
    """Display a question popup and return True for Yes, False for No"""
    logger.verbose_1("Displaying question popup")  # pyright: ignore
    logger.verbose_2(f"{header=}, {footer=}, {body=}")  # pyright: ignore
    height, width = stdscr.getmaxyx()
    popup_height = 5 + len(body)
    popup_width = (
        max(len(header), len(footer), *(len(line) + len(body_prefix) for line in body))
        + 4
    )
    popup_y = (height - popup_height) // 2
    popup_x = (width - popup_width) // 2
    popup_win = stdscr.subwin(popup_height, popup_width, popup_y, popup_x)
    popup_win.clear()
    popup_win.attron(curses.color_pair(5))
    popup_win.bkgd(" ", curses.color_pair(5))
    popup_win.border()
    popup_win.addstr(1, 2, header)
    for idx, line in enumerate(body):
        popup_win.addstr(2 + idx, 2, f"{body_prefix}{line}")
    popup_win.addstr(popup_height - 2, 2, footer)
    popup_win.attroff(curses.color_pair(5))
    popup_win.refresh()

    while True:
        key = stdscr.getch()
        if key in [ord("y"), ord("Y")]:
            popup_win.erase()
            stdscr.clear()
            return True
        elif key in [ord("n"), ord("N")]:
            stdscr.clear()
            return False


def qusetion_popup(stdscr: curses.window, question: str, length: int) -> str:
    """Display a question popup and return user input"""
    logger.verbose_1("Displaying question popup for input")  # pyright: ignore
    logger.verbose_2(f"{question=}")  # pyright: ignore
    height, width = stdscr.getmaxyx()
    popup_height = 5
    popup_width = len(question) + length + 4
    popup_y = (height - popup_height) // 2
    popup_x = (width - popup_width) // 2
    popup_win = stdscr.subwin(popup_height, popup_width, popup_y, popup_x)
    popup_win.clear()
    popup_win.attron(curses.color_pair(5))
    popup_win.bkgd(" ", curses.color_pair(5))
    popup_win.border()
    popup_win.addstr(2, 2, question)
    popup_win.attroff(curses.color_pair(5))
    popup_win.refresh()
    curses.echo()
    user_input = popup_win.getstr(3, 2, length).decode("utf-8")
    curses.noecho()
    popup_win.erase()
    stdscr.clear()
    return user_input


def choice_popup(stdscr: curses.window, opt: ConfigOption) -> str:
    """Display a choice popup for CHOICE type options"""
    logger.verbose_1("Displaying choice popup")  # pyright: ignore
    logger.verbose_2(f"{opt.name=}, {opt.choices=}")  # pyright: ignore
    height, width = stdscr.getmaxyx()
    choice_box = stdscr.subwin(
        len(opt.choices) + 3,
        width - 2,
        height // 2 - (len(opt.choices) // 2),
        2,
    )
    choice_box.border()
    choice_box.addstr(0, 2, f"Select value for {opt.prompt}:")
    for idx, choice in enumerate(opt.choices):
        choice_box.addstr(
            idx + 1,
            2,
            f"{idx + 1}. {choice["name"]} - {choice["description"]}",
        )
    choice_box.refresh()
    curses.echo()
    choice_input = choice_box.getstr(len(opt.choices) + 1, 2, 3).decode("utf-8")
    curses.noecho()
    choice_box.erase()
    stdscr.clear()
    return choice_input


class MenuConfig:
    def __init__(
        self,
        in_file: str = "",
        config_file: str = ".config",
        header_file: str = "autoconf.h",
        editor_enabled: bool = False,
        format_files: bool = False,
    ):
        self.in_file: str = in_file
        if format_files:
            self._format_configs_files()
            return

        self.config: list[ConfigOption] = self._load_config_from_file(in_file)
        self.current_selection: int = 0
        self.scroll_offset: int = 0
        self.show_help: bool = False
        self.current_menu = self.config
        self.menu_stack: list[tuple[list[ConfigOption], int, int]] = []
        self.config_file: str = config_file
        self.header_file: str = header_file
        self.editor_enabled: bool = editor_enabled
        self.load_config()

    def load_config(self):
        """Load configuration from file if it exists"""
        logger.debug("Loading configuration from file")
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, "r") as f:
                    saved = json.load(f)
                self._apply_values(self.config, saved)
            except:
                pass

    def _apply_values(self, options: List[ConfigOption], saved: dict):
        """Recursively apply saved values to options"""
        logger.verbose_1("Applying saved configuration values")  # pyright: ignore
        for opt in options:
            if opt.name in saved:
                opt.value = saved[opt.name]
            if opt.children:
                self._apply_values(opt.children, saved)

    def save_config(self):
        """Save current configuration to file"""
        logger.debug("Saving configuration to file")
        config_dict = {}
        self._collect_values(self.config, config_dict)
        with open(self.config_file, "w") as f:
            json.dump(config_dict, f, indent=2)

        # Also generate a C header file
        self._generate_header(config_dict)

    def _collect_values(self, options: List[ConfigOption], config_dict: dict):
        """Recursively collect all configuration values"""
        logger.verbose_1("Collecting configuration values")  # pyright: ignore
        for opt in options:
            if opt.type != ConfigType.MENU:
                config_dict[opt.name] = opt.value
            if opt.children:
                self._collect_values(opt.children, config_dict)

    def _generate_header(self, config_dict: dict):
        """Generate autoconf.h header file"""
        logger.debug("Generating header file")
        with open(self.header_file, "w") as f:
            f.write("/* Automatically generated - do not edit */\n")
            f.write("#ifndef __AUTOCONF_H\n")
            f.write("#define __AUTOCONF_H\n\n")

            for key, value in config_dict.items():
                if isinstance(value, bool):
                    if value:
                        f.write(f"#define {key} 1\n")
                elif isinstance(value, int):
                    f.write(f"#define {key} {value}\n")
                elif isinstance(value, str):
                    f.write(f'#define {key} "{value}"\n')

            f.write("\n#endif /* __AUTOCONF_H */\n")

    def get_visible_items(self) -> List[ConfigOption]:
        """Get currently visible menu items"""
        logger.verbose_1("Getting visible menu items")  # pyright: ignore
        return [opt for opt in self.current_menu if self._check_show_if(opt)]

    def _flatten_options(self, options: List[ConfigOption]) -> List[ConfigOption]:
        logger.verbose_1("Flattening options for dependency check")  # pyright: ignore
        result = []
        for option in options:
            result.append(option)
            if option.children:
                result.extend(self._flatten_options(option.children))
        logger.verbose_2(f"{result=}")  # pyright: ignore
        return result

    def _check_depends(self, opt: ConfigOption) -> tuple[bool, tuple[str]]:
        """Check if dependencies are satisfied, and if not return missing dependencies"""
        logger.verbose_1(  # pyright: ignore
            f"Checking dependencies for option: {opt.name}"
        )
        if not opt.depends_on:
            return True, ("",)
        collected_deps = []

        all_options = self._flatten_options(self.config)
        name_to_option = {o.name: o for o in all_options}
        for dep in opt.depends_on:
            if dep not in name_to_option:
                collected_deps.append(dep)
                continue
            dep_opt = name_to_option[dep]
            if dep_opt.type == ConfigType.BOOL:
                if not dep_opt.value:
                    collected_deps.append(dep)
            elif dep_opt.type == ConfigType.TRISTATE:
                if dep_opt.value != "y":
                    collected_deps.append(dep)
            else:
                if not dep_opt.value:
                    collected_deps.append(dep)
        if collected_deps:
            logger.verbose_1(  # pyright: ignore
                f"Found {len(collected_deps)} missing for {opt.name}"
            )
            logger.verbose_2(f"{collected_deps=}")  # pyright: ignore
            return False, tuple(collected_deps)
        return True, ("",)

    def _check_show_if(self, opt: ConfigOption) -> bool:
        """Check if show_if conditions are satisfied"""
        logger.verbose_1(  # pyright: ignore
            f"Checking show_if conditions for option: {opt.name}"
        )
        if not opt.show_if:
            return True

        all_options = self._flatten_options(self.config)
        name_to_option = {o.name: o for o in all_options}
        split = lambda c: (c, None) if "." not in c else c.split(".", 1)

        for condition in opt.show_if:
            condition, sub = split(condition)
            if condition not in name_to_option:
                return False
            cond_opt = name_to_option[condition]
            if cond_opt.type == ConfigType.BOOL:
                if not cond_opt.value:
                    return False
            elif cond_opt.type == ConfigType.TRISTATE:
                if cond_opt.value != "y":
                    return False
            elif cond_opt.type == ConfigType.CHOICE:
                if not cond_opt.value == sub:
                    return False
            else:
                if not cond_opt.value:
                    return False
        return True

    def run(self, stdscr: curses.window):
        """Main curses loop"""
        logger.debug("Starting curses main loop")
        curses.curs_set(0)
        pairs = [
            (curses.COLOR_BLACK, curses.COLOR_CYAN),
            (curses.COLOR_WHITE, curses.COLOR_BLUE),
            (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            (curses.COLOR_WHITE, curses.COLOR_RED),
            (curses.COLOR_BLACK, curses.COLOR_YELLOW),
        ]
        logger.verbose_1("Initializing color pairs")  # pyright: ignore
        for i, color in enumerate(pairs, start=1):
            curses.init_pair(i, *color)

        print(type(stdscr))
        while True:
            stdscr.refresh()
            height, width = stdscr.getmaxyx()

            # Draw title
            logger.verbose_1("Drawing title")  # pyright: ignore
            if self.menu_stack:
                title = f"Project Configuration ({self.menu_stack[-1][0][self.menu_stack[-1][1]].prompt})"
            else:
                title = f"Project Configuration (root)"
            stdscr.attron(curses.color_pair(2))
            stdscr.addstr(0, 0, title.center(width))
            stdscr.attroff(curses.color_pair(2))

            # Draw menu items
            logger.verbose_1("Drawing menu items")  # pyright: ignore
            visible_items = self.get_visible_items()
            display_height = height - 6

            for idx, opt in enumerate(visible_items[self.scroll_offset :]):
                if idx >= display_height:
                    break

                if self._check_show_if(opt) is False:
                    continue

                y_pos = idx + 2
                actual_idx = idx + self.scroll_offset

                if actual_idx == self.current_selection:
                    stdscr.attron(curses.color_pair(1))

                # Format the line based on option type
                line = self._format_option_line(opt, width - 4)
                stdscr.addstr(y_pos, 2, line[: width - 4])

                if actual_idx == self.current_selection:
                    stdscr.attroff(curses.color_pair(1))

            # Draw help text
            logger.verbose_1("Drawing help text")  # pyright: ignore
            if visible_items and self.current_selection < len(visible_items):
                current_opt = visible_items[self.current_selection]
                help_y = height - 4
                stdscr.attron(curses.color_pair(3))
                stdscr.addstr(help_y, 2, "Help:")
                stdscr.attroff(curses.color_pair(3))
                help_text = (
                    current_opt.help_text[: width - 4]
                    if current_opt.help_text
                    else "No help available"
                )
                stdscr.addstr(
                    help_y + 1,
                    2,
                    help_text[: width - 4] + " " * (width - 4 - len(help_text)),
                )

            # Draw footer
            logger.verbose_1("Drawing footer")  # pyright: ignore
            footer = f"<Enter>Select/Edit  <Space>Toggle  <S>Save  <Q>Quit  <?>Help{'  <BS>Back' if self.menu_stack else ''}{'  <E>Edit Option  <N>New Option  <D>elete option' if self.editor_enabled else ''}"
            stdscr.attron(curses.color_pair(2))
            stdscr.addstr(height - 2, 0, footer[:width].ljust(width))
            stdscr.attroff(curses.color_pair(2))

            stdscr.refresh()

            # Handle input
            logger.verbose_1("Waiting for user input")  # pyright: ignore
            key = stdscr.getch()
            logger.verbose_2(f"{key=}")  # pyright: ignore

            if key == ord("q") or key == ord("Q"):
                stdscr.addstr(
                    height - 2,
                    2,
                    "To quit press 'q' again to confirm, any other key to cancel.",
                )
                stdscr.refresh()
                key = stdscr.getch()
                if key == ord("q") or key == ord("Q"):
                    break
            elif key == ord("s") or key == ord("S"):
                self.save_config()
                stdscr.addstr(height - 2, 2, "Configuration saved!")
                stdscr.refresh()
                curses.napms(1000)
            elif key in [curses.KEY_UP, ord("k"), ord("K")]:
                if self.current_selection > 0:
                    self.current_selection -= 1
                    if self.current_selection < self.scroll_offset:
                        self.scroll_offset = self.current_selection
            elif key in [curses.KEY_DOWN, ord("j"), ord("J")]:
                if self.current_selection < len(visible_items) - 1:
                    self.current_selection += 1
                    if self.current_selection >= self.scroll_offset + display_height:
                        self.scroll_offset += 1
            elif key == ord(" "):
                self._toggle_option(visible_items[self.current_selection])
                stdscr.clear()
            elif key in [ord("n"), ord("N")] and self.editor_enabled:
                logger.verbose_1("Creating new option")  # pyright: ignore
                self._new_option(stdscr)
            elif key in [ord("e"), ord("E")] and self.editor_enabled:
                logger.verbose_1("Editing current option")  # pyright: ignore
                opt = visible_items[self.current_selection]
                self._edit_option(stdscr, opt)
            elif key in [ord("d"), ord("D")] and self.editor_enabled:
                logger.verbose_1("Deleting current option")  # pyright: ignore
                opt = visible_items[self.current_selection]
                self._delete_option(stdscr, opt)
                self.current_selection = max(0, self.current_selection - 1)
            elif key == curses.KEY_RESIZE:
                logger.verbose_1("Handling terminal resize")  # pyright: ignore
                stdscr.clear()
                y, x = stdscr.getmaxyx()
                curses.resize_term(y, x)
                continue
            elif key == ord("\n") or key == curses.KEY_ENTER:
                logger.verbose_1("Handling Enter key")  # pyright: ignore
                opt = visible_items[self.current_selection]
                satisfied, missing_deps = self._check_depends(opt)
                if not satisfied:
                    logger.debug(
                        f"Dependencies not satisfied for {opt.name}: {missing_deps}"
                    )
                    answer = question_popup(
                        stdscr,
                        "Missing dependencies",
                        "Enable missing dependencies? (y/n)",
                        "- ",
                        *missing_deps,
                    )
                    if not answer:
                        continue
                    for dep_name in missing_deps:
                        logger.verbose_1(  # pyright: ignore
                            f"Enabling dependency: {dep_name}"
                        )
                        for o in self.config:
                            if o.name == dep_name:
                                logger.verbose_2(  # pyright: ignore
                                    f"Found dependency option: {o.name}"
                                )
                                if o.type == ConfigType.BOOL:
                                    logger.verbose_1(  # pyright: ignore
                                        f"Setting {o.name} to True"
                                    )
                                    o.value = True
                                    logger.verbose_2(f"{o.value=}")  # pyright: ignore
                                elif o.type == ConfigType.TRISTATE:
                                    logger.verbose_1(  # pyright: ignore
                                        f"Setting {o.name} to 'y'"
                                    )
                                    o.value = "y"
                                    logger.verbose_2(f"{o.value=}")  # pyright: ignore
                if opt.type in [ConfigType.MENU, ConfigType.DYNAMICMENU]:
                    self._enter_option(visible_items[self.current_selection])
                    stdscr.clear()
                    continue
                logger.verbose_1(f"Editing option: {opt.name}")  # pyright: ignore
                if opt.type == ConfigType.CHOICE:
                    if not opt.choices:
                        error_popup(stdscr, "No choices available for this option")
                        continue
                    choice_input = choice_popup(stdscr, opt)
                    try:
                        choice_idx = int(choice_input) - 1
                        if 0 <= choice_idx < len(opt.choices):
                            opt.value = opt.choices[choice_idx]["name"]
                    except ValueError:
                        pass
                else:
                    edit_box = stdscr.subwin(3, width - 4, height // 2 - 1, 2)
                    edit_box.border()
                    if opt.range:
                        range_text = (
                            f" (Range: {opt.range['min']} - {opt.range['max']})"
                        )
                    else:
                        range_text = ""
                    text = f"Enter new value for {opt.name} type ({opt.type.name}){range_text}:"
                    edit_box.addstr(1, 2, text)
                    curses.echo()
                    edit_box.refresh()
                    new_value = edit_box.getstr(1, len(text) + 3, 20).decode("utf-8")
                    logger.verbose_2(f"{new_value=}")  # pyright: ignore
                    curses.noecho()
                    if new_value == "" or new_value.lower() in [
                        "cancel",
                        "exit",
                        "c",
                    ]:
                        continue
                    if opt.type == ConfigType.STRING:
                        if opt.range:
                            if opt.range["min"] <= len(new_value) <= opt.range["max"]:
                                opt.value = new_value
                            else:
                                error_popup(
                                    stdscr,
                                    f"String length must be between {opt.range['min']} and {opt.range['max']}",
                                )
                        else:
                            opt.value = new_value
                    elif opt.type == ConfigType.INT:
                        try:
                            if opt.range:
                                int_value = int(new_value)
                                if opt.range["min"] <= int_value <= opt.range["max"]:
                                    opt.value = int_value
                                else:
                                    error_popup(
                                        stdscr,
                                        f"The number must be between {opt.range['min']} and {opt.range['max']}",
                                    )
                            else:
                                opt.value = int(new_value)
                        except ValueError:
                            error_popup(stdscr, "Invalid integer value")
                    elif opt.type == ConfigType.CHOICE:
                        if not opt.choices:
                            error_popup(stdscr, "No choices available for this option")
                            continue
                    elif opt.type == ConfigType.BOOL:
                        if new_value.lower() in ["y", "yes", "1", "true"]:
                            opt.value = True
                        elif new_value.lower() in ["n", "no", "0", "false"]:
                            opt.value = False
                    elif opt.type == ConfigType.TRISTATE:
                        if new_value.lower() in ["y", "yes", "1", "true"]:
                            opt.value = "y"
                        elif new_value.lower() in ["m", "module", "mod", "2"]:
                            opt.value = "m"
                        elif new_value.lower() in ["n", "no", "0", "false"]:
                            opt.value = None
                    stdscr.clear()
                    logger.verbose_1("Finished editing option")  # pyright: ignore
                    logger.verbose_2(f"{opt.value=}")  # pyright: ignore
            elif key in [curses.KEY_RIGHT, ord("l"), ord("L")]:
                self._enter_option(visible_items[self.current_selection])
            elif key in [
                curses.KEY_BACKSPACE,
                127,
                ord("l"),
                ord("L"),
                curses.KEY_LEFT,
            ]:
                if self.menu_stack:
                    self.current_menu, self.current_selection, self.scroll_offset = (
                        self.menu_stack.pop()
                    )
                    logger.verbose_1(  # pyright: ignore
                        f"Popped from menu stack: {self.current_menu[0].prompt}"
                    )
                    stdscr.clear()

    def _format_option_line(self, opt: ConfigOption, _):
        """Format a menu line for display"""
        logger.verbose_1(f"Formatting option line: {opt.name}")  # pyright: ignore
        if opt.type == ConfigType.BOOL:
            checkbox = "[*]" if opt.value else "[ ]"
            return f"{checkbox} {opt.prompt}"
        elif opt.type == ConfigType.TRISTATE:
            state = "[*]" if opt.value == "y" else "[M]" if opt.value == "m" else "[ ]"
            return f"{state} {opt.prompt}"
        elif opt.type == ConfigType.STRING or opt.type == ConfigType.INT:
            return f"({opt.value}) {opt.prompt}"
        elif opt.type == ConfigType.MENU or opt.type == ConfigType.DYNAMICMENU:
            return f"{opt.prompt} --->"
        elif opt.type == ConfigType.CHOICE:
            return f"<{opt.value}> {opt.prompt}"
        return opt.prompt

    def _toggle_option(self, opt: ConfigOption):
        """Toggle the value of an option"""
        logger.verbose_1(f"Toggling option: {opt.name}")  # pyright: ignore
        if opt.type == ConfigType.BOOL:
            opt.value = not opt.value
        elif opt.type == ConfigType.TRISTATE:
            states = [None, "m", "y"]
            try:
                idx = states.index(opt.value)
                opt.value = states[(idx + 1) % 3]
            except:
                opt.value = "y"

    def _enter_option(self, opt: ConfigOption):
        """Enter a submenu or edit an option"""
        logger.verbose_1(f"Entering submenu: {opt.name}")  # pyright: ignore
        if opt.children:
            self.menu_stack.append(
                (self.current_menu, self.current_selection, self.scroll_offset)
            )
            logger.verbose_2(  # pyright: ignore
                f"Pushed to menu stack: {self.menu_stack[-1]}"
            )
            self.current_menu = opt.children
            self.current_selection = 0
            self.scroll_offset = 0

    def _delete_option(self, stdscr: curses.window, opt: ConfigOption):
        """Delete an option (not used in main loop)"""
        logger.verbose_1(f"Deleting option: {opt.name}")  # pyright: ignore
        if not question_popup(
            stdscr,
            "Confirm Deletion",
            f"Are you sure you want to delete option '{opt.name}'('{opt.prompt}')? (y/n)",
        ):
            return
        if self.menu_stack:
            logger.verbose_1("Creating new option in submenu")  # pyright: ignore
            current_menu_file = self.menu_stack[-1][0][
                self.menu_stack[-1][1]
            ].source_file
        else:
            current_menu_file = self.in_file
        logger.verbose_1(  # pyright: ignore
            f"Deleting option from: {current_menu_file}"
        )
        # Remove from in-memory config
        self.current_menu.remove(opt)

        # Rewrite the configuration file without the deleted option
        with open(current_menu_file, "r") as f:
            data = yaml.safe_load(f)
        data = [
            entry
            for entry in data
            if not (isinstance(entry, dict) and entry.get("name") == opt.name)
        ]
        with open(current_menu_file, "w") as f:
            yaml.dump(data, f, sort_keys=False)
        error_popup(stdscr, f"Option '{opt.name}' deleted.")
        stdscr.clear()

    def _new_option(self, stdscr: curses.window):
        """Create a new option (not used in main loop)"""
        if self.menu_stack:
            logger.verbose_1("Creating new option in submenu")  # pyright: ignore
            current_menu_file = self.menu_stack[-1][0][
                self.menu_stack[-1][1]
            ].source_file
        else:
            current_menu_file = self.in_file
        logger.verbose_1(  # pyright: ignore
            f"Creating new option in: {current_menu_file}"
        )
        choices = [
            {
                "name": "bool",
                "description": "Boolean option",
                "prompt": "Boolean Option",
                "type": ConfigType.BOOL,
                "default": False,
                "help_text": "A boolean configuration option",
                "depends_on": None,
            },
            {
                "name": "tristate",
                "description": "Tristate option",
                "prompt": "Tristate Option",
                "type": ConfigType.TRISTATE,
                "default": "n",
                "help_text": "A tristate configuration option",
                "depends_on": None,
            },
            {
                "name": "string",
                "description": "String option",
                "prompt": "String Option",
                "type": ConfigType.STRING,
                "range": {"min": 0, "max": 256},
                "default": "",
                "help_text": "A string configuration option",
                "depends_on": None,
            },
            {
                "name": "int",
                "description": "Integer option",
                "prompt": "Integer Option",
                "type": ConfigType.INT,
                "range": {"min": 0, "max": 10000},
                "default": 0,
                "help_text": "An integer configuration option",
                "depends_on": None,
            },
            {
                "name": "choice",
                "description": "Choice option",
                "prompt": "Choice Option",
                "type": ConfigType.CHOICE,
                "choices": [],
                "default": "",
                "help_text": "A choice configuration option",
                "depends_on": None,
            },
            {
                "name": "menu",
                "description": "Submenu",
                "prompt": "Submenu",
                "type": ConfigType.MENU,
                "help_text": "A submenu option",
                "depends_on": None,
                "source": "configs/example_submenu.yaml",
            },
            {
                "name": "dynamicmenu",
                "description": "Dynamic submenu",
                "prompt": "Dynamic Submenu",
                "type": ConfigType.DYNAMICMENU,
                "help_text": "A dynamic submenu option",
                "help_text_fmt": "A dynamic submenu for {filename}",
                "depends_on": None,
                "source": "configs/example/*.yaml",
            },
        ]
        blacklist_attributes = [
            "source_file",
            "children",
            "depends_on",
            "type",
            "description",
        ]
        menu_type = choice_popup(
            stdscr,
            ConfigOption(
                name="menu_type",
                prompt="Select option type:",
                type=ConfigType.CHOICE,
                choices=choices,
            ),
        )
        menu_entry = choices[int(menu_type) - 1]
        logger.verbose_2(f"{menu_type=}")  # pyright: ignore
        logger.verbose_2(f"{menu_entry=}")  # pyright: ignore
        opts_to_ask = [
            key for key in menu_entry.keys() if key not in blacklist_attributes
        ]
        logger.verbose_2(f"{opts_to_ask=}")  # pyright: ignore
        new_option_data = {}
        for opt in opts_to_ask:
            if opt == "range":
                min = qusetion_popup(
                    stdscr,
                    f"Enter minimum value for range (default: {menu_entry[opt]['min']}): ",
                    10,
                )
                max = qusetion_popup(
                    stdscr,
                    f"Enter maximum value for range (default: {menu_entry[opt]['max']}): ",
                    10,
                )
                new_option_data[opt] = {
                    "min": int(min) if min else menu_entry[opt]["min"],
                    "max": int(max) if max else menu_entry[opt]["max"],
                }
                logger.verbose_2(f"{new_option_data[opt]=}")  # pyright: ignore
            elif opt == "choices":
                logger.verbose_1(  # pyright: ignore
                    "Entering choices for CHOICE option"
                )
                while True:
                    choice_name = qusetion_popup(
                        stdscr,
                        "Enter choice name (or 'done' or '' (nothing) to finish): ",
                        40,
                    )
                    if choice_name.lower() == "done" or choice_name == "":
                        break
                    choice_desc = qusetion_popup(
                        stdscr,
                        f"Enter description for choice '{choice_name}': ",
                        60,
                    )
                    if "choices" not in new_option_data:
                        new_option_data["choices"] = []
                    new_option_data["choices"].append(
                        {"name": choice_name, "description": choice_desc}
                    )
            else:
                new_value = qusetion_popup(
                    stdscr, f"Enter value for {opt} (default: {menu_entry[opt]}): ", 40
                )
                new_option_data[opt] = new_value
        new_option = ConfigOption(
            name=new_option_data.get("name", "new_option"),
            prompt=new_option_data.get("prompt", "New Option"),
            type=menu_entry["type"],
            default=new_option_data.get("default"),
            help_text=new_option_data.get("help_text", ""),
            help_text_fmt=new_option_data.get("help_text_fmt", ""),
            depends_on=new_option_data.get("depends_on"),
            choices=new_option_data.get("choices", []),
            range=new_option_data.get("range"),
        )
        if new_option.type == ConfigType.INT and new_option.default is not None:
            new_option.default = int(new_option.default)
        logger.verbose_2(f"{new_option=}")  # pyright: ignore
        if self.menu_stack:
            self.menu_stack[-1][0][self.menu_stack[-1][1]].children.append(new_option)
        else:
            self.config.append(new_option)
        with open(current_menu_file, "a") as f:
            f.write("\n")
            yaml.dump(
                [
                    {
                        k: v
                        for k, v in {
                            "name": new_option.name,
                            "prompt": new_option.prompt,
                            "type": new_option.type.value,
                            "default": new_option.default,
                            "help_text": new_option.help_text,
                            "help_text_fmt": new_option.help_text_fmt,
                            "depends_on": new_option.depends_on,
                            "choices": new_option.choices,
                            "range": new_option.range,
                        }.items()
                        if v
                    }
                ],
                f,
                sort_keys=False,
            )
        error_popup(stdscr, "New option created and added to configuration file.")

    def _edit_option(self, stdscr: curses.window, opt: ConfigOption):
        """Edit an option value (not used in main loop)"""
        logger.verbose_1(f"Editing option: {opt.name}")  # pyright: ignore
        props: list = [
            x
            for x in [
                {
                    "name": "prompt",
                    "description": "Prompt Text",
                },
                {
                    "name": "type",
                    "description": "Option Type",
                },
                {
                    "name": "help_text",
                    "description": "Help Text",
                },
                (
                    {
                        "name": "help_text_fmt",
                        "description": "Help Text Format",
                    }
                    if opt.type == ConfigType.DYNAMICMENU
                    else None
                ),
                {
                    "name": "default",
                    "description": "Default Value",
                },
                {
                    "name": "depends_on",
                    "description": "Dependencies (comma separated)",
                },
                {
                    "name": "range",
                    "description": "Range (min,max)",
                },
                (
                    {
                        "name": "choices",
                        "description": "Choices (for CHOICE type)",
                    }
                    if opt.type == ConfigType.CHOICE
                    else None
                ),
                (
                    {
                        "name": "source",
                        "description": "Source File (for MENU/DYNAMICMENU types)",
                    }
                    if opt.type in [ConfigType.MENU, ConfigType.DYNAMICMENU]
                    else None
                ),
                {
                    "name": "done",
                    "description": "Finish Editing",
                },
            ]
            if x
        ]
        while True:
            choice_idx = choice_popup(
                stdscr,
                ConfigOption(
                    name="edit_property",
                    prompt="Select property to edit:",
                    type=ConfigType.CHOICE,
                    choices=props,
                ),
            )
            try:
                idx = int(choice_idx) - 1
                if idx < 0 or idx >= len(props):
                    continue
                if props[idx]["name"] == "done":
                    break
                new_value = qusetion_popup(
                    stdscr,
                    f"Enter new value for {props[idx]['description']} (current: {getattr(opt, props[idx]['name'])}): ",
                    60,
                )
                if props[idx]["name"] == "depends_on":
                    opt.depends_on = (
                        [s.strip() for s in new_value.split(",")] if new_value else None
                    )
                elif props[idx]["name"] == "range":
                    if new_value:
                        min_str, max_str = new_value.split(",")
                        opt.range = {
                            "min": int(min_str.strip()),
                            "max": int(max_str.strip()),
                        }
                    else:
                        opt.range = None
                elif props[idx]["name"] == "choices":
                    logger.verbose_1(  # pyright: ignore
                        "Editing choices for CHOICE option"
                    )
                    choices = []
                    while True:
                        choice_name = qusetion_popup(
                            stdscr,
                            "Enter choice name (or 'done' or '' (nothing) to finish): ",
                            40,
                        )
                        if choice_name.lower() == "done" or choice_name == "":
                            break
                        choice_desc = qusetion_popup(
                            stdscr,
                            f"Enter description for choice '{choice_name}': ",
                            60,
                        )
                        choices.append(
                            {"name": choice_name, "description": choice_desc}
                        )
                    opt.choices = choices
                else:
                    logger.verbose_1(  # pyright: ignore
                        f"Setting {props[idx]['name']} to {new_value}"
                    )
                    setattr(opt, props[idx]["name"], new_value)
            except ValueError:
                continue

        if opt.type == ConfigType.INT and opt.default is not None:
            opt.default = int(opt.default)
        if self.menu_stack:
            logger.verbose_1("Creating new option in submenu")  # pyright: ignore
            current_menu_file = self.menu_stack[-1][0][
                self.menu_stack[-1][1]
            ].source_file
        else:
            current_menu_file = self.in_file
        logger.verbose_1(f"Updating option in: {current_menu_file}")  # pyright: ignore
        # Rewrite the configuration file with the updated option
        with open(current_menu_file, "r") as f:
            data = yaml.safe_load(f)
        for entry in data:
            if isinstance(entry, dict) and entry.get("name") == opt.name:
                entry.update(
                    {
                        k: v
                        for k, v in {
                            "prompt": opt.prompt,
                            "type": opt.type.value,
                            "default": opt.default,
                            "help_text": opt.help_text,
                            "help_text_fmt": opt.help_text_fmt,
                            "depends_on": opt.depends_on,
                            "choices": opt.choices,
                            "range": opt.range,
                        }.items()
                        if v
                    }
                )
        logger.verbose_2(f"{data=}")  # pyright: ignore
        with open(current_menu_file, "w") as f:
            yaml.dump(data, f, sort_keys=False)
        self._format_configs_files()

    def _format_configs_files(self):
        """Format configuration files (not used in main loop)"""
        logger.verbose_1("Formatting configuration files")  # pyright: ignore
        start = time2.perf_counter_ns()
        logger.debug(f"Starting formatting at {start}")
        processed = set()

        def load_and_format_file(path: Path):
            path = path.resolve()
            if path in processed:
                return
            processed.add(path)

            with path.open("r", encoding="utf-8") as f:
                content = f.read()

            data = yaml.safe_load(content)

            # --- Handle recursive menu/dynamicmenu sources ---
            logger.verbose_2(f"{data=}")  # pyright: ignore
            for entry in data:
                if isinstance(entry, dict) and entry.get("type") in (
                    "menu",
                    "dynamicmenu",
                ):
                    source = entry.get("source")
                    if source:
                        for src_file in self._expand_sources(path.parent, source):
                            load_and_format_file(src_file)

            # --- Reformat YAML text ---
            formatted_content = self._format_yaml_arrays(content)

            with path.open("w", encoding="utf-8") as f:
                f.write(formatted_content)

        # Start at root
        load_and_format_file(Path(self.in_file))
        logger.debug(f"Finished formatting at {time()}")
        logger.info(
            f"Formatted {len(processed)} files in {timedelta(microseconds=(time2.perf_counter_ns()-start))/1000} seconds"
        )

    def _expand_sources(self, base_path: Path, source: str):
        """
        Expands a source entry into actual files.
        Supports direct file references or glob patterns.
        """
        source_path = base_path / source
        if "*" in source or "?" in source or "[" in source:
            # Glob pattern
            return [Path(p) for p in glob(str(source_path))]
        else:
            return [source_path]

    def _format_yaml_arrays(self, raw_text: str) -> str:
        """
        Inserts a blank line before each top level YAML list item except the first.
        This is a TEXT-LEVEL operation to avoid modifying the logical YAML data.
        """
        logger.verbose_2(f"{raw_text=}")  # pyright: ignore
        lines = raw_text.splitlines()
        formatted = []
        fomratted_c = 0

        for line in lines:
            if line.startswith("- name: "):
                if fomratted_c:
                    line = f"\n{line}"
                fomratted_c += 1
            formatted.append(line)

        return "\n".join(formatted) + "\n"

    def _load_config_from_file(
        self, filename: str, depth: int = 0
    ) -> List[ConfigOption]:
        """Load configuration definition from a YAML file"""
        logger.debug(f"Loading configuration from file: {filename}")
        if depth > 5:
            raise RecursionError("Maximum menu depth exceeded")
        with open(filename, "r") as f:
            data = yaml.safe_load(f)
        if depth == 0:
            self.old_cwd = os.getcwd()
            os.chdir(os.path.dirname(os.path.abspath(filename)))

        def parse_option(opt_dict):
            opt_type = ConfigType(opt_dict["type"])
            if opt_type == ConfigType.MENU:
                logger.verbose_1(  # pyright: ignore
                    f"Loading submenu from {opt_dict['source']} for {opt_dict['name']}"
                )
                return ConfigOption(
                    name=opt_dict["name"],
                    prompt=opt_dict["prompt"],
                    type=opt_type,
                    help_text=opt_dict.get("help_text", ""),
                    depends_on=opt_dict.get("depends"),
                    range=opt_dict.get("range", None),
                    source_file=opt_dict.get("source", ""),
                    show_if=opt_dict.get("show_if", None),
                    children=(
                        self._load_config_from_file(opt_dict["source"], depth=depth + 1)
                        if "source" in opt_dict
                        else []
                    ),
                )
            elif opt_type == ConfigType.DYNAMICMENU:
                logger.verbose_1(  # pyright: ignore
                    f"Loading submenus from {opt_dict['source']} for {opt_dict['name']}"
                )
                children = []
                for file in glob(opt_dict["source"]):
                    logger.verbose_2(  # pyright: ignore
                        f"Loading dynamic submenu from file: {file}"
                    )
                    children.append(
                        ConfigOption(
                            name=os.path.basename(file).split(".")[0],
                            prompt=os.path.basename(file).split(".")[0],
                            type=ConfigType.MENU,
                            children=self._load_config_from_file(file, depth=depth + 1),
                            source_file=file,
                            help_text=opt_dict.get("help_text_fmt", "").format(
                                filename=os.path.basename(file).split(".")[0]
                            ),
                            show_if=opt_dict.get("show_if", None),
                        )
                    )
                return ConfigOption(
                    name=opt_dict["name"],
                    prompt=opt_dict["prompt"],
                    type=opt_type,
                    help_text=opt_dict.get("help_text", ""),
                    depends_on=opt_dict.get("depends_on"),
                    range=opt_dict.get("range", None),
                    source_file=opt_dict.get("source", ""),
                    show_if=opt_dict.get("show_if", None),
                    children=children,
                )
            return ConfigOption(
                name=opt_dict["name"],
                prompt=opt_dict["prompt"],
                type=opt_type,
                default=opt_dict.get("default"),
                help_text=opt_dict.get("help_text", ""),
                depends_on=opt_dict.get("depends_on"),
                choices=opt_dict.get("choices", []),
                range=opt_dict.get("range", None),
                show_if=opt_dict.get("show_if", None),
                source_file=opt_dict.get("source", ""),
            )

        tmp = [parse_option(opt) for opt in data]
        if depth == 0:
            os.chdir(self.old_cwd)
        return tmp


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Menuconfig-style Configuration System"
    )
    parser.add_argument(
        "--config",
        type=str,
        default="config.yaml",
        help="Path to configuration definition file",
    )
    parser.add_argument(
        "--output",
        type=str,
        default=".config",
        help="Path to save the configuration file",
    )
    parser.add_argument(
        "--header",
        type=str,
        default="autoconf.h",
        help="Path to save the generated header file",
    )
    parser.add_argument(
        "--rm",
        action="store_true",
        help="Remove existing configuration file before starting",
    )
    parser.add_argument(
        "--enable-editor",
        action="store_true",
        help="Enable configuration option editor (not implemented yet)",
    )
    parser.add_argument(
        "--format",
        action="store_true",
        help="Format configuration definition files and exit",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Enable debug logging",
    )
    parser.add_argument(
        "--logfile",
        type=str,
        default="conf_debug.log",
        help="Path to debug log file",
    )
    parser.add_argument(
        "-v", "--verbose", action="count", default=0, help="Increase verbosity level"
    )
    args = parser.parse_args()
    MAX_VERBOSE_LEVEL = 2
    args.verbose = min(args.verbose, MAX_VERBOSE_LEVEL)

    def make_verbose_logger(level):
        def verbose_logger(self, message, *args, **kwargs):
            if self.isEnabledFor(level):
                self._log(level, message, args, **kwargs, stacklevel=2)

        return verbose_logger

    for i in range(1, MAX_VERBOSE_LEVEL + 1):
        logging.addLevelName(logging.DEBUG - i, f"VERBOSE_{i}")
        setattr(logging, f"VERBOSE_{i}", logging.DEBUG - i)
        setattr(
            logging.getLoggerClass(),
            f"verbose_{i}",
            make_verbose_logger(logging.DEBUG - i),
        )
        setattr(
            logging,
            f"log_verbose_{i}",
            lambda message, *args, **kwargs: logging.log(
                logging.DEBUG - i, message, *args, **kwargs
            ),
        )

    logging.basicConfig(
        filename=f"log/{date.today()}.log",
        filemode="w",
        level=logging.DEBUG,
        format="%(asctime)s-%(filename)s:%(lineno)s %(levelname)s %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    logger = logging.getLogger("menuconfig")
    fh = logging.FileHandler(args.logfile)
    logger.addHandler(fh)
    if args.debug:
        logger.setLevel(logging.DEBUG)
        if args.verbose:
            logger.setLevel(logging.DEBUG - args.verbose)
    else:
        logger.setLevel(logging.INFO)

    logger.debug("Starting menuconfig")
    logger.verbose_1(f"{args=}")  # pyright: ignore
    logger.verbose_2(f"{sys.argv=}")  # pyright: ignore

    if args.rm and os.path.exists(args.output):
        logging.info(f"Removing existing configuration file: {args.output}")
        os.remove(args.output)
    if args.rm and os.path.exists(args.header):
        logging.info(f"Removing existing header file: {args.header}")
        os.remove(args.header)

    logging.info(f"Loading configuration definition from: {args.config}")
    menu = MenuConfig(
        in_file=args.config,
        config_file=args.output,
        header_file=args.header,
        editor_enabled=args.enable_editor,
        format_files=args.format,
    )
    if args.format:
        print("Configuration definition files formatted.")
        sys.exit(0)

    try:
        curses.wrapper(menu.run)
        print("\nConfiguration complete!")
    except KeyboardInterrupt:
        print("\nConfiguration cancelled")
    except Exception as e:
        logger.error(f"Exception occurred: {e}", exc_info=True)
        print(f"\nError: {e}")
        import traceback

        traceback.print_exc()
# Vim: set expandtab tabstop=4 shiftwidth=4:
