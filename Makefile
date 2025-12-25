CONFIG_FILE := ".config"

menuconfig:
	@scripts/conf.py --config configs/main.yaml --output ${CONFIG_FILE} --header "autoconf.h" --rm --debug -vv --enable-editor

newmodules:
	@scripts/conf.py --config configs/new_module.yaml --output "/tmp/new_mod.json" --header "/tmp/new_mod.h" --debug -vv
	@sed -i 's/#define \(CONFIG_NEW_MODULE_NAME\) "\(.*\)"/&\n#define CONFIG_NEW_MODULE_UP_NAME "\U\2"/' /tmp/new_mod.h
	@clang -ggdb -c -std=c23 scripts/new_module.c -o /tmp/new_module.o -D__MAX_SIZE=$$(du -ab modules/template | sort -rh | head -n 1 | awk '{print $$1}')
	@clang -ggdb /tmp/new_module.o -o /tmp/new_module
	@/tmp/new_module

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
