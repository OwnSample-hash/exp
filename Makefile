CONFIG_FILE := ".config"

all: flow

@flow = menuconfig gen_plugins_inc build
include chains.mk

flow: @flow
	@printf "Done!\n"

menuconfig menuconfig@flow:
	@scripts/conf.py --config configs/main.yaml --output ${CONFIG_FILE} --header "include/config.h" --rm --debug -vv --enable-editor

newmodules:
	@scripts/conf.py --config configs/new_module.yaml --output "/tmp/new_mod.json" --header "/tmp/new_mod.h" --debug -vv
	@scripts/new_module.py --config /tmp/new_mod.json
	@${MAKE} gen_modules_inc

gen_plugins_inc gen_plugins_inc@flow:
	@mkdir -p include
	@printf "/**\n* @file\n* @brief Automaticly includes plugin headers. Generated via 'make gen_plugins_inc'\n* This file is auto-generated. Do not edit!\n*/\n" > include/plugins.hpp
	@echo "#pragma once" >> include/plugins.hpp
	@for i in $$(find plugins/ -type f -wholename "*/include/*.hpp"); do \
		if [[ "$$i" == *"template"* ]]; then continue; fi; \
		echo "#include \"../$$i\"" >> include/plugins.hpp; \
	done

build build@flow:
	@mkdir -p build
	@cmake -S . -B build -G "Ninja"
	@cmake --build build
