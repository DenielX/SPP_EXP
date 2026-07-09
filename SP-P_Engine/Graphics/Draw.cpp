#include "SP-P_Engine/Graphics/Draw.h"
#include "SP-P_Engine/Graphics/Obj.h"
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>

int g_draw_calls_this_frame = 0;
bool g_enable_culling = true;
unsigned int __attribute__((aligned(16))) list[1024];//под обновление камеры
// Выделяем 1mb под команды GE
unsigned int __attribute__((aligned(16))) static_world_list[262144];
int g_total_baked_draw_calls = 0; // Для сохранения логов


// =======================================================
// ЕДИНОРАЗОВАЯ НАСТРОЙКА ОСВЕЩЕНИЯ И СТЕЙТОВ
// =======================================================
void InitRenderStates() {
	// Настройка освещения
	sceGuDisable(GU_LIGHTING);
	sceGuEnable(GU_LIGHT0);
	sceGuEnable(GU_NORMALIZED_NORMAL);
	sceGuShadeModel(GU_SMOOTH);
	sceGuAmbientColor(0xFFCCCCCC);

	ScePspFVector3 sunDir = { -0.9701f, 0.0f, 0.2425f };
	sceGuLight(0, GU_DIRECTIONAL, GU_AMBIENT | GU_DIFFUSE, &sunDir);
	sceGuLightColor(0, GU_DIFFUSE, 0xFFFFFFFF);
	sceGuLightColor(0, GU_AMBIENT, 0xFFCCCCCC);

	sceGuColorMaterial(GU_AMBIENT | GU_DIFFUSE);
	sceGuMaterial(GU_AMBIENT | GU_DIFFUSE, 0xFFFFFFFF);

	// Базовые стейты (Z-буфер, Куллинг, Цвет очистки)
	sceGuEnable(GU_CLIP_PLANES);
	sceGuEnable(GU_DEPTH_TEST);
	sceGuDepthFunc(GU_LEQUAL);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuEnable(GU_CULL_FACE);
	sceGuFrontFace(GU_CCW);
	sceGuClearColor(0xFFFFB466);
	sceGuClearDepth(65535);

	// По умолчанию для непрозрачных (глухих) объектов
	sceGuDisable(GU_BLEND);
	sceGuEnable(GU_ALPHA_TEST);
	sceGuAlphaFunc(GU_GREATER, 0x10, 0xFF); // Отсекаем пиксели с прозрачностью (дырки в заборах)
	sceGuDepthMask(GU_FALSE); // Разрешаем запись в Z-буфер
}

void BakeStaticWorldList() {
	// Начинаем запись в наш статический массив
	sceGuStart(GU_CALL, static_world_list);

	// Сброс стейтов перед началом кадра (запишется в начало списка)
	sceGuDisable(GU_LIGHTING);
	sceGuDisable(GU_BLEND);
	sceGuEnable(GU_ALPHA_TEST);
	sceGuDepthMask(GU_FALSE);
	sceGuEnable(GU_CULL_FACE);
	sceGuFrontFace(GU_CCW);

	ClumpTEX* last_bound_tex_ptr = nullptr;
	unsigned int i = 0;
	bool is_lighting_enabled = false;
	bool is_culling_disabled = false;

	int vertex_format;
	int prim_type;
	unsigned int vertex_stride;

	g_total_baked_draw_calls = 0;

	// Извлекаем масштаб один раз, чтобы не дергать указатели в цикле
	float wrld_scale = 1.0f;
	if (drawlist[0].MDL_clump->wrld_radius != nullptr) {
		wrld_scale = *drawlist[0].MDL_clump->wrld_radius;
	}

	while (i < countobj) {
		unsigned int current_instances = drawlist[i].IPL_struct->count_instances;
		int flag = (int)(*drawlist[i].MDL_clump->flags[0]);

		prim_type = (flag == 0) ? GU_TRIANGLE_STRIP : ((flag == 1) ? GU_TRIANGLE_STRIP : GU_TRIANGLES);
		vertex_format = (flag == 0) ? (GU_TEXTURE_8BIT | GU_COLOR_5551 | GU_VERTEX_16BIT | GU_TRANSFORM_3D) :
			(GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D);
		vertex_stride = (flag == 0) ? 10 : 36;

		if (i == ALPHA_START_INDEX) {
			if (is_lighting_enabled) {
				sceGuDisable(GU_LIGHTING);
				is_lighting_enabled = false;
			}
			sceGuEnable(GU_BLEND);
			sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
			last_bound_tex_ptr = nullptr;
		}

		IMG* img = drawlist[i].IMG_model_ptr;
		if (img == nullptr || img->vaddr == nullptr) {
			i += current_instances;
			continue;
		}


		unsigned short num_materials = *drawlist[i].MDL_clump->count_materials_or_index_of_material[0];
		unsigned int geom_offset = *drawlist[i].MDL_clump->mdl_size;

		// Компенсация за глобальный 16-байтный заголовок в формате WRLD
		if (flag == 0) {
			geom_offset -= 16;
		}

		// --- Применение динамического освещения (только для глухих объектов) ---
		if (i < ALPHA_START_INDEX) {
			if (*drawlist[i].MDL_clump->dyn_ligting_on[0] != is_lighting_enabled) {
				if (*drawlist[i].MDL_clump->dyn_ligting_on[0]) {
					sceGuEnable(GU_LIGHTING);
				} else {
					sceGuDisable(GU_LIGHTING);
				}
				is_lighting_enabled = *drawlist[i].MDL_clump->dyn_ligting_on[0];
			}
		}

		if (*drawlist[i].MDL_clump->dis_cull[0] != is_culling_disabled) {
			if (*drawlist[i].MDL_clump->dis_cull[0]) {
				sceGuDisable(GU_CULL_FACE);
			} else {
				sceGuEnable(GU_CULL_FACE);
			}
			is_culling_disabled = *drawlist[i].MDL_clump->dis_cull[0];
		}

		for (int m0 = num_materials - 1; m0 >= 0; m0--) {
			unsigned int countInSub = *drawlist[i].MDL_clump->count_vertices_in_submesh[m0];
			if (countInSub <= 0) continue;

			geom_offset -= countInSub * vertex_stride;
			void* current_vertex_ptr = (void*)((char*)img->vaddr + geom_offset);

			// --- Аппаратная распаковка WRLD (UV и Координаты) ---
			if (flag == 0) {
				// Восстановление UV (Аппаратная матрица текстуры)
				float uv_range = *drawlist[i].MDL_clump->uv_range[m0];
				float min_u = *drawlist[i].MDL_clump->min_u[m0];
				float min_v = *drawlist[i].MDL_clump->min_v[m0];
				float tex_scale = (128.0f / 255.0f) * uv_range;
				sceGuTexScale(tex_scale, tex_scale);
				sceGuTexOffset(min_u, min_v);
			} else {
				sceGuTexScale(1.0f, 1.0f);
				sceGuTexOffset(0.0f, 0.0f);
			}

			// --- Биндинг Текстур ---
			unsigned int tex_idx = *drawlist[i].MDL_clump->texture_index[m0];
			if (tex_idx != 0xFFFF) {
				ClumpTEX* tex = &TEX_clump[tex_idx];
				if (tex != last_bound_tex_ptr && tex->data != nullptr) {
					int gu_fmt = GU_PSM_8888;
					if (tex->pix_form == 0x00) gu_fmt = GU_PSM_5650;
					else if (tex->pix_form == 0x01) gu_fmt = GU_PSM_5551;
					else if (tex->pix_form == 0x02) gu_fmt = GU_PSM_4444;
					else if (tex->pix_form == 0x04) gu_fmt = GU_PSM_T4;
					else if (tex->pix_form == 0x05) gu_fmt = GU_PSM_T8;

					sceGuTexMode(gu_fmt, 0, 0, tex->pix_swizzle);

					if ((gu_fmt == GU_PSM_T4 || gu_fmt == GU_PSM_T8) && tex->pal_data != nullptr) {
						int clut_fmt = (tex->pal_form == 0x03) ? GU_PSM_8888 : GU_PSM_4444;
						sceGuClutMode(clut_fmt, 0, 0xFF, 0);
						unsigned int bytes_per_color = (tex->pal_form == 0x03) ? 4 : 2;
						unsigned int blocks = (tex->pal_count * bytes_per_color) / 32;
						sceGuClutLoad(blocks, tex->pal_data);
					}

					sceGuTexImage(0, tex->width, tex->height, tex->width, tex->data);
					sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
					sceGuTexFilter(GU_NEAREST, GU_NEAREST);//можно и включить (был GU_LINEAR)
					last_bound_tex_ptr = tex;
				}
			}

			// =======================================================
			// 2. ОТРИСОВКА (чистый цикл для всех типов)
			// =======================================================
			for (unsigned int inst = i; inst < i + current_instances; ++inst) {
				ScePspFMatrix4 __attribute__((aligned(16))) aligned_matrix;
				memcpy(&aligned_matrix, drawlist[inst].IPL_struct->rotation_matrix, sizeof(ScePspFMatrix4));

				// Применяем масштаб только для кусков карты (WRLD)
				if (flag == 0) {
					aligned_matrix.x.x *= wrld_scale; aligned_matrix.x.y *= wrld_scale; aligned_matrix.x.z *= wrld_scale;
					aligned_matrix.y.x *= wrld_scale; aligned_matrix.y.y *= wrld_scale; aligned_matrix.y.z *= wrld_scale;
					aligned_matrix.z.x *= wrld_scale; aligned_matrix.z.y *= wrld_scale; aligned_matrix.z.z *= wrld_scale;
				}

				sceGuSetMatrix(GU_MODEL, &aligned_matrix);

				// Записываем команду отрисовки геометрии
				sceGuDrawArray(prim_type, vertex_format, countInSub, nullptr, current_vertex_ptr);

				g_total_baked_draw_calls++;
			}
		} // Конец цикла материалов (m0)

		i += current_instances;
	} // Конец цикла по объектам

	sceGuFinish(); // ПРАВИЛЬНОЕ РАСПОЛОЖЕНИЕ: Финиш ВНЕ главного цикла!
}

// =======================================================
// ГЛАВНЫЙ ЦИКЛ ОТРИСОВКИ (ВЫЗЫВАЕТСЯ КАЖДЫЙ КАДР!)
// =======================================================
void OnDraw()
{
	g_draw_calls_this_frame = g_total_baked_draw_calls;

	sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

	// Обновляем матрицу проекции и позицию камеры
	sceGumMatrixMode(GU_PROJECTION);
	sceGumLoadIdentity();
	sceGumPerspective(fov, 480.0f / 272.0f, front_plane, draw_dist);

	Camera();
	sceGumUpdateMatrix();

	// УЛЬТИМАТИВНАЯ ОПТИМИЗАЦИЯ CPU:
	// Просто говорим видеочипу выполнить запеченный список в ОЗУ.
	sceGuCallList(static_world_list);
}