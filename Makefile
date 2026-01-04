CONFIG_FILE := ".config"

menuconfig:
	@scripts/conf.py --config configs/main.yaml --output ${CONFIG_FILE} --header "autoconf.h" --rm --debug -vv --enable-editor

newmodules:
	@scripts/conf.py --config configs/new_module.yaml --output "/tmp/new_mod.json" --header "/tmp/new_mod.h" --debug -vv
	@sed -i 's/#define \(CONFIG_NEW_MODULE_NAME\) "\(.*\)"/&\n#define CONFIG_NEW_MODULE_UP_NAME "\U\2"/' /tmp/new_mod.h
	@ln -sf template_`jq ".CONFIG_NEW_MODULE_TYPE" /tmp/new_mod.json -r`.hpp modules/template/include/template.hpp
	@ln -sf template_`jq ".CONFIG_NEW_MODULE_TYPE" /tmp/new_mod.json -r`.cpp modules/template/src/template.cpp
	@ln -sf vargs_`jq ".CONFIG_NEW_MODULE_TYPE" /tmp/new_mod.json -r`.txt modules/template/include/vargs.txt
	@ln -sf vargs_`jq ".CONFIG_NEW_MODULE_TYPE" /tmp/new_mod.json -r`.txt modules/template/src/vargs.txt
	@clang -ggdb -c -std=c23 scripts/new_module.c -o /tmp/new_module.o \
		-D__MAX_SIZE=$$(du -ab modules/template | sort -rh | head -n 1 | awk '{print $$1}') \
		-DTEMPLATE_CPP_VARGS="$$(cat modules/template/src/vargs.txt | tr '\n' ' ')" \
		-DTEMPLATE_HPP_VARGS="$$(cat modules/template/include/vargs.txt | tr '\n' ' ')"
	@clang -ggdb /tmp/new_module.o -o /tmp/new_module
	@/tmp/new_module
	@rm /tmp/new_module.o /tmp/new_module \
		modules/template/include/template.hpp modules/template/src/template.cpp \
		modules/template/include/vargs.txt modules/template/src/vargs.txt

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
