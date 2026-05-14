# KT Animal Detection SDK Component Makefile
# This is a pre-compiled SDK, no build needed

INSTALL_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))/install
COMPONENT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

.PHONY: all install clean

all:
	@echo "KT Animal Detection SDK - pre-compiled, no build needed"

install:
	@echo "Installing KT Animal Detection SDK..."
	@mkdir -p $(INSTALL_DIR)/lib
	@mkdir -p $(INSTALL_DIR)/include
	@mkdir -p $(INSTALL_DIR)/bin/resource
	@mkdir -p $(INSTALL_DIR)/bin/certs
	@cp -d $(COMPONENT_DIR)/lib/*.so* $(INSTALL_DIR)/lib/ 2>/dev/null || true
	@cp $(COMPONENT_DIR)/include/*.h $(INSTALL_DIR)/include/
	@cp $(COMPONENT_DIR)/bin/get_cpuid $(INSTALL_DIR)/bin/ 2>/dev/null || true
	@cp $(COMPONENT_DIR)/bin/user_config.json $(INSTALL_DIR)/bin/ 2>/dev/null || true
	@cp $(COMPONENT_DIR)/bin/resource/* $(INSTALL_DIR)/bin/resource/ 2>/dev/null || true
	@cp $(COMPONENT_DIR)/bin/certs/* $(INSTALL_DIR)/bin/certs/ 2>/dev/null || true
	@echo "KT Animal Detection SDK installed"

clean:
	@rm -rf $(INSTALL_DIR)
	@echo "KT Animal Detection SDK cleaned"
