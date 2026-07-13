#include "Texture.h"
#include <cstring>
#include <malloc.h>      // Для memalign
#include <pspkernel.h>   // Для сброса кэша

ClumpTEX* ParserTEX(unsigned char* texture, ClumpTEX* TEX_clump_arg) {

	TEX_clump_arg->next_ofst = (unsigned int*)(texture + 20);
	TEX_clump_arg->colors = 4;

	unsigned int off = 16;
	if (*(unsigned short*)(texture + off) == 0x02) {
		off += *(unsigned int*)(texture + off + 8);
	}

	unsigned int pic_child_off = 0;
	char* ext_name_ptr = nullptr;

	// --- ШАГ А: Ищем Picture (0x03) и Info (0xFF) ---
	unsigned int curr_level_chunk = off;
	while (curr_level_chunk > 0) {
		if ((unsigned int)(texture + curr_level_chunk) % 2 != 0) break;

		unsigned short c_type = *(unsigned short*)(texture + curr_level_chunk);
		unsigned int c_next = *(unsigned int*)(texture + curr_level_chunk + 4);
		unsigned int c_child = *(unsigned int*)(texture + curr_level_chunk + 8);
		unsigned int c_data = *(unsigned int*)(texture + curr_level_chunk + 12);

		if (c_type == 0x03) {
			pic_child_off = curr_level_chunk + c_child;
		} else if (c_type == 0xFF) {
			ext_name_ptr = (char*)(texture + curr_level_chunk + c_data);
			break;
		} else if (c_type != 0x02) {
			break;
		}

		if (c_next < 16) break;
		curr_level_chunk += c_next;
	}

	if (pic_child_off == 0) return NULL;

	// --- ШАГ Б: Спускаемся внутрь Picture и ищем Растр (0x04) и Палитру (0x05) ---
	unsigned char* pal_data = nullptr;
	unsigned int pal_count = 0;
	unsigned short pal_form = 0;

	// ВРЕМЕННЫЙ указатель на сырые пиксели внутри файла
	unsigned char* raw_pixels = nullptr;

	unsigned int curr_chunk = pic_child_off;
	while (curr_chunk > 0) {
		if ((unsigned int)(texture + curr_chunk) % 2 != 0) break;

		unsigned short c_type = *(unsigned short*)(texture + curr_chunk);
		unsigned int c_next = *(unsigned int*)(texture + curr_chunk + 4);
		unsigned int c_data = *(unsigned int*)(texture + curr_chunk + 12);

		if (c_type != 0x04 && c_type != 0x05) {
			break;
		}

		if (c_type == 0x04) {
			unsigned int inner_hdr_size = *(unsigned short*)(texture + curr_chunk + c_data);
			unsigned int skip_offst = inner_hdr_size + 16;

			TEX_clump_arg->pix_form = *(unsigned short*)(texture + curr_chunk + 0x14);
			TEX_clump_arg->pix_swizzle = *(unsigned short*)(texture + curr_chunk + 0x16);
			TEX_clump_arg->width = *(unsigned short*)(texture + curr_chunk + 0x18);
			TEX_clump_arg->height = *(unsigned short*)(texture + curr_chunk + 0x1A);

			// Запоминаем, где лежат пиксели в загруженном файле
			raw_pixels = texture + curr_chunk + c_data + skip_offst;

		} else if (c_type == 0x05) {
			unsigned int inner_hdr_size = *(unsigned short*)(texture + curr_chunk + c_data);
			unsigned int skip_offst = inner_hdr_size + 16;

			pal_form = *(unsigned short*)(texture + curr_chunk + 0x14);
			pal_count = *(unsigned short*)(texture + curr_chunk + 0x18);
			pal_data = texture + curr_chunk + c_data + skip_offst;
		}

		if (c_next < 16) break;
		curr_chunk += c_next;
	}

	// --- 2. ОПРЕДЕЛЕНИЕ РАЗМЕРОВ И ФОРМАТОВ ---
	switch (TEX_clump_arg->pix_form) {
	case 0x00: // PSM_5650
		TEX_clump_arg->colors = 2;
		TEX_clump_arg->size = TEX_clump_arg->width * TEX_clump_arg->height * 2;
		break;
	case 0x01: // PSM_5551
		TEX_clump_arg->colors = 2;
		TEX_clump_arg->size = TEX_clump_arg->width * TEX_clump_arg->height * 2;
		break;
	case 0x02: // PSM_4444
		TEX_clump_arg->colors = 2;
		TEX_clump_arg->size = TEX_clump_arg->width * TEX_clump_arg->height * 2;
		break;
	case 0x03: // PSM_8888
		TEX_clump_arg->colors = 4;
		TEX_clump_arg->size = TEX_clump_arg->width * TEX_clump_arg->height * 4;
		break;
	case 0x04: // T4 (Палитра)
		TEX_clump_arg->colors = 1;
		TEX_clump_arg->size = (TEX_clump_arg->width * TEX_clump_arg->height) / 2;
		break;
	case 0x05: // T8 (Палитра)
		TEX_clump_arg->colors = 1;
		TEX_clump_arg->size = TEX_clump_arg->width * TEX_clump_arg->height;
		break;
	default:
		return NULL;
	}

	// --- 3. ВЫДЕЛЕНИЕ ПАМЯТИ И КОПИРОВАНИЕ ПИКСЕЛЕЙ ---
	if (raw_pixels != nullptr) {
		TEX_clump_arg->data = (unsigned char*)memalign(16, TEX_clump_arg->size);
		if (TEX_clump_arg->data != nullptr) {
			memcpy(TEX_clump_arg->data, raw_pixels, TEX_clump_arg->size);
			sceKernelDcacheWritebackInvalidateRange(TEX_clump_arg->data, TEX_clump_arg->size);
		}
	} else {
		TEX_clump_arg->data = nullptr;
	}

	// --- 3.5 ВЫДЕЛЕНИЕ ПАМЯТИ ПОД ПАЛИТРУ (ФИКС ВАРНИНГОВ И БАГА) ---
	TEX_clump_arg->pal_form = pal_form;
	TEX_clump_arg->pal_count = pal_count;

	if (pal_data != nullptr && pal_count > 0) {
		// Если pal_form == 0x03, то 1 цвет весит 4 байта (RGBA8888), иначе 2 байта
		unsigned int pal_size = pal_count * ((pal_form == 0x03) ? 4 : 2);
		TEX_clump_arg->pal_data = (unsigned char*)memalign(16, pal_size);

		if (TEX_clump_arg->pal_data != nullptr) {
			memcpy(TEX_clump_arg->pal_data, pal_data, pal_size);
			sceKernelDcacheWritebackInvalidateRange(TEX_clump_arg->pal_data, pal_size);
		}
	} else {
		TEX_clump_arg->pal_data = nullptr;
	}

	// --- 4. ЧТЕНИЕ ИМЕНИ ---
	TEX_clump_arg->name[0] = new char[32];
	if (ext_name_ptr != nullptr) {
		strncpy(TEX_clump_arg->name[0], ext_name_ptr, 31);
	} else {
		strncpy(TEX_clump_arg->name[0], (char*)(texture + 144 + TEX_clump_arg->size), 31);
	}
	TEX_clump_arg->name[0][31] = '\0';

	// TODO в будущем: Если texture.pix_form == 0x04 или 0x05, 
	// нужно аналогично через memalign скопировать pal_data в структуру TEX_clump
	// для отправки в CLUT регистры чипа GE.

	return TEX_clump_arg;
}