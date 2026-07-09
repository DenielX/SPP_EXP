#pragma once

#include "SP-P_Engine/Misc/Keys.h"

extern int g_draw_calls_this_frame; // логирование кол-ва вызовов отрисовки для текущего кадра
extern bool g_enable_culling; // флаг вкл/выкл софтварного куллинга+отсечения по дальности
extern unsigned int static_world_list[262144]; // память под команды GE
extern unsigned int list[1024];//под обновление камеры

void InitRenderStates();
void BakeStaticWorldList();
void OnDraw();
