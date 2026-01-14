CONFIG_FILE := ".config"

menuconfig:
	@scripts/conf.py --config configs/main.yaml --output ${CONFIG_FILE} --header "include/config.h" --rm --debug -vv --enable-editor

newmodules:
	@scripts/conf.py --config configs/new_module.yaml --output "/tmp/new_mod.json" --header "/tmp/new_mod.h" --debug -vv
	@scripts/new_module.py --config /tmp/new_mod.json

gen_modules_inc:
	@mkdir -p include
	@echo "// This file is auto-generated. Do not edit!" > include/modules.hpp
	@echo "#pragma once" >> include/modules.hpp
	@for i in $$(find modules/ -type f -wholename "*/include/*.hpp"); do \
		if [[ "$$i" == *"template"* ]]; then continue; fi; \
		echo "#include \"../$$i\"" >> include/modules.hpp; \
	done

build:
	@mkdir -p build
	@cmake -S . -B build -G "Ninja"
	@cmake --build build
