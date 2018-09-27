ifeq ($(OPENWRT_BUILD),)

#Normal system
CFLAGS=-MMD -O2 -Wall -I/opt/local/include -g
LDFLAGS=-L/opt/local/lib
LDLIBS=-lusb-1.0 -lm
CC=gcc

HMLAN_OBJS=hmcfgusb.o hmland.o util.o
HMSNIFF_OBJS=hmcfgusb.o hmuartlgw.o hmsniff.o
FLASH_HMCFGUSB_OBJS=hmcfgusb.o firmware.o util.o flash-hmcfgusb.o
FLASH_HMMODUART_OBJS=hmuartlgw.o firmware.o util.o flash-hmmoduart.o
FLASH_OTA_OBJS=hmcfgusb.o culfw.o hmuartlgw.o firmware.o util.o flash-ota.o hm.o aes.o

OBJS=$(HMLAN_OBJS) $(HMSNIFF_OBJS) $(FLASH_HMCFGUSB_OBJS) $(FLASH_HMMODUART_OBJS) $(FLASH_OTA_OBJS)

all: hmland hmsniff flash-hmcfgusb flash-hmmoduart flash-ota

DEPEND=$(OBJS:.o=.d)
-include $(DEPEND)

hmland: $(HMLAN_OBJS)

hmsniff: $(HMSNIFF_OBJS)

flash-hmcfgusb: $(FLASH_HMCFGUSB_OBJS)

flash-hmmoduart: $(FLASH_HMMODUART_OBJS)

flash-ota: $(FLASH_OTA_OBJS)

clean:
	rm -f $(HMLAN_OBJS) $(HMSNIFF_OBJS) $(FLASH_HMCFGUSB_OBJS) $(FLASH_HMMODUART_OBJS) $(FLASH_OTA_OBJS) $(DEPEND) hmland hmsniff flash-hmcfgusb flash-hmmoduart flash-ota

.PHONY: all clean

else

#OpenWRT/LEDE
include $(TOPDIR)/rules.mk

PKG_NAME:=hmcfgusb
PKG_VERSION:=$(shell grep 'VERSION' version.h | cut -d'"' -f 2)
PKG_RELEASE:=1
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/hmcfgusb
  SECTION:=utils
  CATEGORY:=Utilities
  DEPENDS:=+libusb-1.0
  TITLE:=HM-CFG-USB utilities
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./* $(PKG_BUILD_DIR)/
	$(SED) 's/OPENWRT_BUILD/DISABLED_CHECK_FOR_OPENWRT_TO_USE_CORRECT_BLOCK_NOW/' $(PKG_BUILD_DIR)/Makefile
endef

define Package/hmcfgusb/install
	$(INSTALL_DIR) $(1)/usr/sbin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/hmland $(1)/usr/sbin/
	$(INSTALL_DIR) $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/hmsniff $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/flash-hmcfgusb $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/flash-ota $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc/init.d/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/init.hmland.OpenWRT $(1)/etc/init.d/hmland
endef

define Package/hmcfgusb/postinst
#!/bin/sh
# check if we are on real system
if [ -z "$${IPKG_INSTROOT}" ]; then
	echo "Enabling rc.d symlink for hmland"
	/etc/init.d/hmland enable
fi
exit 0
endef

define Package/hmcfgusb/prerm
#!/bin/sh
# check if we are on real system
if [ -z "$${IPKG_INSTROOT}" ]; then
	echo "Removing rc.d symlink for hmland"
	/etc/init.d/hmland disable
fi
exit 0
endef

$(eval $(call BuildPackage,hmcfgusb))
endif
