CONFIG_FILE := ".config"

menuconfig:
	@scripts/conf.py --config configs/main.yaml --output ${CONFIG_FILE} --header "autoconf.h" --rm --debug -vv --enable-editor

newmodules:
	@scripts/conf.py --config configs/new_module.yaml --output "new_mod.json" --header "/dev/null" --debug -vv

gen_modules_inc:
	@mkdir -p include
	@echo "// This file is auto-generated. Do not edit!" > include/modules.hpp
	@echo "#pragma once" >> include/modules.hpp
	@for i in $$(find modules/ -type f -wholename "*/include/*.hpp"); do \
		echo "#include \"../$$i\"" >> include/modules.hpp; \
	done

build:
	@mkdir -p build
	@cmake -S . -B build -G "Ninja"
	@cmake --build build
