#include "SP-P_Engine/Graphics/Draw.h"
#include "SP-P_Engine/Graphics/Obj.h"
#include "SP-P_Engine/Misc/Callbacks.h"
#include <pspdisplay.h>
#include <pspgu.h>

//debug:
#include <psprtc.h>

using namespace std;

extern "C" {
	void VramSetSize(int kb);
}

unsigned int size_of_VRAM = 0;
SceUID VRAM_hook;
char VRAM_module[32] = "vramext.prx";

PSP_MODULE_INFO("SA_pre11", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_VFPU | THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(-1);

#define MAX_LOG_FRAMES 60
int log_draw_calls[MAX_LOG_FRAMES];
float log_cpu_ms[MAX_LOG_FRAMES];
float log_gpu_ms[MAX_LOG_FRAMES];
int current_log_frame = 0;
bool log_written = false;

void WriteProfilerLog() {
	if (log_written) return;

	FILE* f = fopen("draw_stats_ms.txt", "w");
	if (f) {
		fprintf(f, "--- ПРОФИЛИРОВЩИК PSP (АБСОЛЮТНОЕ ВРЕМЯ В МИЛЛИСЕКУНДАХ) ---\n");
		fprintf(f, "Бюджет на 1 кадр для 60 FPS = 16.6 ms. Для 30 FPS = 33.3 ms.\n\n");
		fprintf(f, "Frame | Draw Calls | CPU Time (ms) | GPU Time (ms) | Total (ms)\n");
		fprintf(f, "----------------------------------------------------------------\n");

		for (int i = 0; i < MAX_LOG_FRAMES; ++i) {
			fprintf(f, " %02d   |    %04d    |    %8.3f   |    %8.3f   |   %8.3f\n",
				i,
				log_draw_calls[i],
				log_cpu_ms[i],
				log_gpu_ms[i],
				(log_cpu_ms[i] + log_gpu_ms[i]));
		}
		fclose(f);
	}
	log_written = true;
}

int main()
{
	SetupCallbacks();

	VRAM_hook = pspSdkLoadStartModule(VRAM_module, PSP_MEMORY_PARTITION_KERNEL);

	// --- ДИНАМИЧЕСКИЙ ЗАПРОС ПАМЯТИ ---
	#ifdef PSP_SLIM
		VramSetSize(4096); // Просим 4MB для Slim+
	#else
		VramSetSize(2048); // Оставляем стандартные 2MB для Fat
	#endif

	scePowerSetClockFrequency(333, 333, 166);

	sceGuInit();
	sceGuStart(GU_DIRECT, list);

	sceGuDrawBuffer(GU_PSM_8888, (void*)0, 512);
	sceGuDispBuffer(480, 272, (void*)0x88000, 512);
	sceGuDepthBuffer((void*)0x110000, 512);

	//sceGuOffset(2048 - (512 / 2), 2048 - (320 / 2));
	sceGuOffset(2048 - (480 / 2), 2048 - (272 / 2));
	sceGuViewport(2048, 2048, 480, 272);
	//sceGuViewport(2048, 2048, 512, 320);
	sceGuDepthRange(0, 65535);

	sceGuScissor(0, 0, 480, 272);
	sceGuEnable(GU_SCISSOR_TEST);

	// ОДИН РАЗ НАСТРАИВАЕМ ОСВЕЩЕНИЕ И БАЗОВЫЕ СТЕЙТЫ
	InitRenderStates();

	sceGuFinish();
	sceGuSync(0, 0);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);

	MasteringOBJ();
	BakeStaticWorldList();

	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

	for (unsigned int i = 0; i < 256; ++i) {
		keys[i] = false;
	}

	while (1) {
		// #debug
		unsigned int vcount_start = sceDisplayGetVcount();
		// #debug
		uint64_t t_start, t_cpu_done, t_gpu_done;
		sceRtcGetCurrentTick(&t_start);

		OnJoystick();
		sceGuStart(GU_DIRECT, list);
		OnDraw();
		sceGuFinish();

		// #debug
		// 2. ЗАМЕР ВРЕМЕНИ ЗАВЕРШЕНИЯ РАБОТЫ CPU
		sceRtcGetCurrentTick(&t_cpu_done);

		// Ждем, пока видеочип аппаратно отрисует всё, что мы ему передали
		sceGuSync(0, 0);

		// #debug
		// 3. ЗАМЕР ВРЕМЕНИ ЗАВЕРШЕНИЯ РАБОТЫ GPU
		sceRtcGetCurrentTick(&t_gpu_done);

		if (current_log_frame < MAX_LOG_FRAMES) {
			log_draw_calls[current_log_frame] = g_draw_calls_this_frame;

			// sceRtcGetTickResolution() обычно возвращает 1 000 000 (тиков в секунду)
			// Переводим микросекунды в миллисекунды:
			float res = (float)sceRtcGetTickResolution() / 1000.0f;

			log_cpu_ms[current_log_frame] = (t_cpu_done - t_start) / res;
			log_gpu_ms[current_log_frame] = (t_gpu_done - t_cpu_done) / res;

			++current_log_frame;
			if (current_log_frame == MAX_LOG_FRAMES) WriteProfilerLog();
		}

		while (sceDisplayGetVcount() <= vcount_start) {
			sceDisplayWaitVblankStart();
		}

		sceGuSwapBuffers();
	}

	sceKernelExitGame();
	return 0;
}