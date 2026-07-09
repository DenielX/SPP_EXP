TARGET = SPP_EXPv11
OBJS = SP-P_Engine/Misc/vram_hook/VramExt.o \
SP-P_Engine/Misc/Callbacks.o \
SP-P_Engine/Graphics/Camera.o \
SP-P_Engine/Misc/Keys.o \
SP-P_Engine/Parsers/Model.o \
SP-P_Engine/Parsers/Texture.o \
SP-P_Engine/Parsers/IPL.o \
SP-P_Engine/Parsers/IDE.o \
SP-P_Engine/Graphics/Obj.o \
SP-P_Engine/Parsers/IMG.o \
SP-P_Engine/Graphics/Draw.o \
main.o

INCDIR = $(PSPPATH)/include
#подключение инклудов нашего движка
INCDIR += ./SP-P_Engine
#флаги оптимизации при сборке-надо по эксперементировать,-O0-O3-чем выше,тем оптимизированей.G0-вырубает отладочную инфу,Wall-компилятор выводит предупреждения
#-mpreferred-stack-boundary=X,выравнивает стаки кеша проца на 2^X байт,-enable-newlib-hw-fp-задействует Floating point CPU.
CFLAGS = -O3 -G0 -g -Wall -mpreferred-stack-boundary=4 -ffast-math -fno-rtti -fno-exceptions
#-fno-strict-aliasing

# --- КОНФИГУРАЦИЯ СБОРКИ (FAT / SLIM) ---
ifeq ($(MODEL), SLIM)
    # Включаем 64MB RAM в EBOOT для Slim
    PSP_LARGE_MEMORY = 1
    # Передаем макрос PSP_SLIM в cpp код
    CFLAGS += -DPSP_SLIM
else
    # По умолчанию собираем для FAT
    CFLAGS += -DPSP_FAT
endif
# ----------------------------------------

CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)

#для создания PRX
BUILD_PRX = 0
#PSP_FW_VERSION=371-будет работать для прошивок 3.71 и выше,ядро то же
#PSP_LARGE_MEMORY = 1-расширяет выделеную память до 52MB на PSP 2000 и более поздних моделях.тег только для .pbp
PSP_FW_VERSION=371

LIBDIR =
LDFLAGS = -g
LIBS = -lpspgum -lpspgu -lpspdisplay -lpspge -lm -lc -lpspsdk -lpspvfpu -lpspuser -lpspkernel -lpsprtc -lpsppower -lstdc++ -lpspkubridge
#-lpthread-psp

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = SPP_EXPv11

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
