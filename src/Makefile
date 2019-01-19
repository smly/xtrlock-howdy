## miniconf makefiles ## 1.1 ##

TOPDIR := ..

xtrlock-howdy_src = xtrlock.c auth_pam.c
xtrlock-howdy_cflags += $(X11_CFLAGS)
xtrlock-howdy_libs += $(X11_LIBS) -lpam
xtrlock-howdy_type = bin

include $(TOPDIR)/.config/rules.mk
