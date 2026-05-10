CONFIG_FILE := ".config"
.SHELLFLAGS += -o pipefail -e -x

all: flow

#@flow = menuconfig gen_plugins_inc build
@flow = menuconfig build
include chains.mk

flow: @flow
	@printf "Done!\n"

menuconfig menuconfig@flow:
	@if [[ ! -f ${CONFIG_FILE} ]]; then \
		scripts/conf.py --config configs/main.yaml --output ${CONFIG_FILE} --header "include/config.h" --rm --debug -vv --enable-editor; \
	fi

newmodules:
	@scripts/conf.py --config configs/new_module.yaml --output "/tmp/new_mod.json" --header "/tmp/new_mod.h" --debug -vv
	@scripts/new_module.py --config /tmp/new_mod.json
	# @${MAKE} gen_plugins_inc

gen_plugins_inc gen_plugins_inc@flow:
	@mkdir -p include
	@printf "/**\n* @file\n* @brief Automaticly includes plugin headers. Generated via 'make gen_plugins_inc'\n* This file is auto-generated. Do not edit!\n*/\n" > include/plugins.hpp
	@echo "#pragma once" >> include/plugins.hpp
	@for i in $$(find plugins/ -type f -wholename "*/include/*.hpp"); do \
		if [[ "$$i" == *"template"* ]]; then continue; fi; \
		if [[ $$(echo $$i | grep -E ".+/include/.+\.hpp" | sed -E "s#.+/(.+/include/.+)#\1#" | tr "/" "\n" | tr "." "\n" | grep  -E "include|hpp" -v | uniq | wc -l ) == 1 ]]; then \
			echo "#include \"../$$i\"" >> include/plugins.hpp; \
		fi \
	done

build build@flow:
	@mkdir -p build
	@if [[ -x "$(which ninja)" ]]; then \
		GENERATOR="Ninja"; \
	else \
		GENERATOR="Unix Makefiles"; \
	fi; \
	if [[ ! -d build ]]; then \
		cmake -S . -B build -G "$$GENERATOR" -DCMAKE_BUILD_TYPE=Release; \
	else \
		cmake -S . -B build;  \
	fi; 
	@cmake --build build
