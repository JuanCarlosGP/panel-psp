TARGET = panelpsp
OBJS = main.o panel.o button_test.o stick_test.o language.o

CFLAGS = -O2 -Wall -Wextra -Werror
LIBS = -lpsppower -lpspge -lpspdisplay -lpspctrl -lpspwlan -lpsputility -lpspkubridge -lpspsystemctrl_user -lm
BUILD_PRX = 1
PSP_FW_VERSION = 661
PSP_LARGE_MEMORY = 0

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Panel PSP
PSP_EBOOT_ICON = ICON0.PNG

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

panel.o: font.inc panel.h button_test.h
main.o button_test.o: panel.h button_test.h
main.o panel.o stick_test.o: stick_test.h
main.o panel.o button_test.o language.o: language.h
EBOOT.PBP: ICON0.PNG
