#include "MKS_draw_inFile.h"
#include "../SDCard.h"
#include "../SettingsDefinitions.h"
#include "MKS_SDCard.h"
#include "MKS_draw_language.h"
#include <esp_task_wdt.h>

#include <stdlib.h>
#include <string.h>

inFILE_PAGE_T infile_page;

LV_IMG_DECLARE(X_N);			
LV_IMG_DECLARE(X_P);			
LV_IMG_DECLARE(Y_N);			
LV_IMG_DECLARE(Y_P);
LV_IMG_DECLARE(back);

LV_IMG_DECLARE(png_infile_cave);
LV_IMG_DECLARE(png_infile_pos);
LV_IMG_DECLARE(png_infile_frame);

LV_IMG_DECLARE(Positionting);
LV_IMG_DECLARE(png_pos_pre);
	
LV_IMG_DECLARE(png_m_up);
LV_IMG_DECLARE(png_m_right);		
LV_IMG_DECLARE(png_m_left);		
LV_IMG_DECLARE(png_m_down);

LV_IMG_DECLARE(png_infile_pos_pre);
LV_IMG_DECLARE(png_infile_frame_pre);
LV_IMG_DECLARE(png_infile_cave_pre);

LV_IMG_DECLARE(png_m_z_n);			
LV_IMG_DECLARE(png_m_z_n_pre);		
LV_IMG_DECLARE(png_m_z_p);			
LV_IMG_DECLARE(png_m_z_p_pre);
LV_IMG_DECLARE(png_home_pre);
LV_IMG_DECLARE(png_hhome_pre);		
LV_IMG_DECLARE(png_unlock_pre);		
LV_IMG_DECLARE(png_pos_pre);	
LV_IMG_DECLARE(png_m_up);
LV_IMG_DECLARE(png_m_right);		
LV_IMG_DECLARE(png_m_left);		
LV_IMG_DECLARE(png_m_down);
LV_IMG_DECLARE(png_back_pre);	




enum {

	ID_INF_UP,
	ID_INF_DOWN,
	ID_INF_LEFT,
	ID_INF_RIGHT,
	ID_INF_Z_UP,
	ID_INF_Z_DOWN,
	ID_INF_STEP,
	ID_INF_SPEED,
	ID_INF_XY_POS,
	ID_INF_Z_POS,
	ID_INF_CARVE,
	ID_INF_BACK,
	ID_INF_XY_HOME,
	ID_INF_Z_HOME,
	ID_INF_L_NEXT,
	ID_INF_L_UP,
	ID_INF_KNIFE,
	ID_INF_PREVIEW,
};

#define PREVIEW_DRAW_POINTS_MAX  3000
#define PREVIEW_MAX_SEGS         150
#define PREVIEW_Y_BUCKET_MAX     260
#define PREVIEW_ROW_SPAN_MAX     4
#define PREVIEW_POWER_LEVELS     8
#define PREVIEW_BG_R             0x0B
#define PREVIEW_BG_G             0x10
#define PREVIEW_BG_B             0x12

static lv_obj_t* preview_popup = NULL;
static lv_obj_t* preview_area  = NULL;
// Un lv_line par segment laser, jusqu'à PREVIEW_MAX_SEGS
static lv_obj_t* preview_lines[PREVIEW_MAX_SEGS];
static int        preview_lines_count = 0;

static lv_point_t preview_draw_pts[PREVIEW_DRAW_POINTS_MAX];
// Métadonnées par segment : index de début + nombre de points (bas) + puissance (haut)
static struct { uint16_t start; uint32_t count; } preview_segs[PREVIEW_MAX_SEGS];
static int preview_seg_total = 0; // nombre réel de segments parsés
static int preview_y_moves_total = 0;
static uint8_t preview_y_span_count[PREVIEW_Y_BUCKET_MAX];
static int16_t preview_y_span_min[PREVIEW_Y_BUCKET_MAX][PREVIEW_ROW_SPAN_MAX];
static int16_t preview_y_span_max[PREVIEW_Y_BUCKET_MAX][PREVIEW_ROW_SPAN_MAX];
static uint8_t preview_y_span_pwr[PREVIEW_Y_BUCKET_MAX][PREVIEW_ROW_SPAN_MAX];
static lv_color_t preview_power_colors[PREVIEW_POWER_LEVELS];
static float preview_max_s = 100.0f;
static int preview_file_w_um = 100, preview_file_h_um = 100;

static lv_style_t preview_popup_style;
static lv_style_t preview_area_style;
static lv_style_t preview_close_rel;
static lv_style_t preview_close_pr;
static lv_obj_t* preview_wait_label = NULL;
static lv_obj_t* preview_stat1_label = NULL;
static lv_obj_t* preview_stat2_label = NULL;
static lv_task_t* preview_poll_task = NULL;

// Parseur en tranches (state machine LVGL, pas de FreeRTOS)
#define PREVIEW_PS_IDLE     0
#define PREVIEW_PS_PASS1    1
#define PREVIEW_PS_SCALE    2
#define PREVIEW_PS_PASS2    3
#define PREVIEW_PS_FINALIZE 4
#define PREVIEW_PS_RENDER   5
#define PREVIEW_LINES_PER_TICK 80
#define PREVIEW_RENDER_SEGS_PER_TICK 12
#define PREVIEW_SEG_POINTS_MAX 48
#define PREVIEW_BITMAP_MAX_PIXELS 42000

static lv_obj_t* preview_bitmap_img = NULL;
static lv_img_dsc_t preview_bitmap_dsc;
static uint8_t* preview_bitmap_buf = NULL;
static int pp_bmp_w = 0, pp_bmp_h = 0;

static int     pp_state    = PREVIEW_PS_IDLE;
static bool    pp_abort    = false;
static File    pp_file;
static char    pp_fullpath[136] = "";
static int     pp_w = 0, pp_h = 0, pp_lc = 0;
// Etat passe 1
static float   pp_cur_x = 0, pp_cur_y = 0;
static float   pp_min_x = 1e9f, pp_max_x = -1e9f, pp_min_y = 1e9f, pp_max_y = -1e9f;
static bool    pp_has_bounds = false, pp_absolute = true, pp_laser_en = false;
static float   pp_modal_s = 0;
static bool    pp_in_laser_seg = false;
// Echelle
static float   pp_scale = 1.0f, pp_off_x = 0, pp_off_y = 0;
static int     pp_scale_permille = 1000;
static bool    pp_dense_mode = false;
static int     pp_seg_step = 1;
// Etat passe 2
static int     pp_out_n = 0, pp_seg_idx = -1, pp_kept_n = 0;
static bool    pp_keep_cur = false, pp_in_seg2 = false;
static int     pp_y_rows_used = 0;
static int     pp_render_i = 0, pp_render_created = 0;
static bool    pp_render_oom = false;

static void preview_bitmap_free(void) {
	if (preview_bitmap_img) {
		lv_obj_del(preview_bitmap_img);
		preview_bitmap_img = NULL;
	}
	if (preview_bitmap_buf) {
		free(preview_bitmap_buf);
		preview_bitmap_buf = NULL;
	}
	pp_bmp_w = 0;
	pp_bmp_h = 0;
	memset(&preview_bitmap_dsc, 0, sizeof(preview_bitmap_dsc));
}

static bool preview_bitmap_alloc(int w, int h) {
	preview_bitmap_free();
	if (w < 8 || h < 8) return false;

	int bw = w;
	int bh = h;

	const uint32_t palette_bytes = 256U * 4U;
	uint32_t pixels = (uint32_t)bw * (uint32_t)bh;
	uint32_t total = palette_bytes + pixels;

	preview_bitmap_buf = (uint8_t*)malloc(total);
	while (!preview_bitmap_buf && bw > 32 && bh > 24) {
		bw /= 2;
		bh /= 2;
		pixels = (uint32_t)bw * (uint32_t)bh;
		total = palette_bytes + pixels;
		preview_bitmap_buf = (uint8_t*)malloc(total);
	}
	if (!preview_bitmap_buf) return false;

	memset(preview_bitmap_buf, 0, total);

	const uint8_t bg_b = PREVIEW_BG_B;
	const uint8_t bg_g = PREVIEW_BG_G;
	const uint8_t bg_r = PREVIEW_BG_R;
	for (int i = 0; i < 256; i++) {
		preview_bitmap_buf[i * 4 + 0] = bg_b;
		preview_bitmap_buf[i * 4 + 1] = bg_g;
		preview_bitmap_buf[i * 4 + 2] = bg_r;
		preview_bitmap_buf[i * 4 + 3] = 0xFF;
	}
	for (int i = 0; i < PREVIEW_POWER_LEVELS; i++) {
		lv_color_t c = preview_power_colors[i];
		// c.ch.blue = 5 bits (0-31), c.ch.green = 6 bits (0-63), c.ch.red = 5 bits (0-31)
		// La palette INDEXED_8BIT attend des valeurs 8 bits (BGRA)
		preview_bitmap_buf[(i + 1) * 4 + 0] = (uint8_t)((c.ch.blue  * 255u) / 31u);
		preview_bitmap_buf[(i + 1) * 4 + 1] = (uint8_t)((c.ch.green * 255u) / 63u);
		preview_bitmap_buf[(i + 1) * 4 + 2] = (uint8_t)((c.ch.red   * 255u) / 31u);
		preview_bitmap_buf[(i + 1) * 4 + 3] = 0xFF;
	}

	preview_bitmap_dsc.header.always_zero = 0;
	preview_bitmap_dsc.header.w = (uint16_t)bw;
	preview_bitmap_dsc.header.h = (uint16_t)bh;
	preview_bitmap_dsc.header.cf = LV_IMG_CF_INDEXED_8BIT;
	preview_bitmap_dsc.data_size = total;
	preview_bitmap_dsc.data = preview_bitmap_buf;
	pp_bmp_w = bw;
	pp_bmp_h = bh;
	return true;
}

static void preview_bitmap_plot_line(int x0, int y0, int x1, int y1, uint8_t pwr_level) {
	if (!preview_bitmap_buf || pp_bmp_w <= 0 || pp_bmp_h <= 0) return;
	if (pwr_level >= PREVIEW_POWER_LEVELS) pwr_level = PREVIEW_POWER_LEVELS - 1;
	uint8_t idx = (uint8_t)(pwr_level + 1);
	uint8_t* pix = preview_bitmap_buf + (256U * 4U);

	int dx = abs(x1 - x0);
	int sx = (x0 < x1) ? 1 : -1;
	int dy = -abs(y1 - y0);
	int sy = (y0 < y1) ? 1 : -1;
	int err = dx + dy;

	for (;;) {
		if ((unsigned)x0 < (unsigned)pp_bmp_w && (unsigned)y0 < (unsigned)pp_bmp_h) {
			uint8_t* p = &pix[y0 * pp_bmp_w + x0];
			if (idx > *p) *p = idx;
		}
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

static void preview_info_popup_cb(lv_obj_t* obj, lv_event_t event) {
	if (event == LV_EVENT_RELEASED) {
		common_popup_com_del();
	}
}

// Extrait la valeur d'un axe (X ou Y) depuis une ligne G-code
static bool parse_axis_value(const char* line, char axis, float* out_val) {
	if (!line || !out_val) return false;
	const char axis_l = (char)(axis + ('a' - 'A'));
	const char* p = line;
	while (*p) {
		if (*p == axis || *p == axis_l) {
			// Vérifier que ce n'est pas une lettre dans un mot (ex: "FEED")
			if (p == line || !isalpha((unsigned char)*(p-1))) {
				char* endp = NULL;
				float v = strtof(p + 1, &endp);
				if (endp && endp != (p + 1)) {
					*out_val = v;
					return true;
				}
			}
		}
		p++;
	}
	return false;
}

// Vérifie qu'une ligne contient un mouvement G0/G00/G1/G01 avec X ou Y
static bool line_has_motion_xy(const char* line) {
	if (!line) return false;
	// Chercher Gx ou gx où x est 0 ou 1 (suivi de 0, 1, espace, ou fin)
	bool has_motion = false;
	const char* p = line;
	while (*p) {
		if ((*p == 'G' || *p == 'g') && (*(p+1) == '0' || *(p+1) == '1')) {
			char next = *(p+2);
			// G0 ou G1 ou G00 ou G01 — pas G10, G12, etc.
			if (*(p+1) == '0') {
				// G0: accepté si suivi de espace/tab/fin/X/Y/Z/F ou d'un 0 ou 1 (G00/G01) puis non-digit
				if (next == ' ' || next == '\t' || next == '\0' || next == 'X' || next == 'Y' || next == 'x' || next == 'y') {
					has_motion = true; break;
				}
				if ((next == '0' || next == '1') && (!isdigit((unsigned char)*(p+3)))) {
					has_motion = true; break;
				}
			} else { // G1
				if (next == ' ' || next == '\t' || next == '\0' || next == 'X' || next == 'Y' || next == 'x' || next == 'y') {
					has_motion = true; break;
				}
				// G01 — suivi d'un non-digit
				if (next == '0' && (!isdigit((unsigned char)*(p+3)))) {
					has_motion = true; break;
				}
			}
		}
		p++;
	}
	if (!has_motion) return false;
	// Doit aussi contenir une coordonnée X ou Y
	if (strstr(line, "X") || strstr(line, "Y") || strstr(line, "x") || strstr(line, "y")) return true;
	return false;
}

// Retourne 0=G0, 1=G1, -1=autre (vérifie aussi présence X ou Y)
static int get_motion_type(const char* line) {
	if (!line) return -1;
	if (!strstr(line, "X") && !strstr(line, "Y") && !strstr(line, "x") && !strstr(line, "y")) return -1;
	const char* p = line;
	while (*p) {
		if (*p == 'G' || *p == 'g') {
			char c1 = *(p+1), c2 = *(p+2);
			if (c1 == '0') {
				if (!isdigit((unsigned char)c2)) return 0;               // G0x
				if (c2 == '0' && !isdigit((unsigned char)*(p+3))) return 0; // G00
				if (c2 == '1' && !isdigit((unsigned char)*(p+3))) return 1; // G01
			} else if (c1 == '1' && !isdigit((unsigned char)c2)) return 1; // G1x
		}
		p++;
	}
	return -1;
}

// Debug: reason for failure (filled by build_preview_points)
static char preview_debug_msg[80] = "";

static void init_power_colors(void) {
	// Palette: bleu (bas) → vert → rouge (haut) en 8 niveaux
	preview_power_colors[0] = LV_COLOR_MAKE(0x00, 0x00, 0xFF); // Bleu
	preview_power_colors[1] = LV_COLOR_MAKE(0x00, 0x55, 0xFF); // Bleu-cyan
	preview_power_colors[2] = LV_COLOR_MAKE(0x00, 0xAA, 0xFF); // Cyan
	preview_power_colors[3] = LV_COLOR_MAKE(0x00, 0xFF, 0x7F); // Cyan-vert
	preview_power_colors[4] = LV_COLOR_MAKE(0x00, 0xFF, 0x00); // Vert
	preview_power_colors[5] = LV_COLOR_MAKE(0xFF, 0xFF, 0x00); // Jaune
	preview_power_colors[6] = LV_COLOR_MAKE(0xFF, 0x88, 0x00); // Orange
	preview_power_colors[7] = LV_COLOR_MAKE(0xFF, 0x00, 0x00); // Rouge
}

static uint8_t power_to_level(float s_value, float s_max) {
	if (s_max <= 0) s_max = 100.0f;
	float norm = s_value / s_max;
	if (norm < 0.01f) return 0; // Presque off → bleu
	if (norm > 1.0f) norm = 1.0f;
	uint8_t level = (uint8_t)(norm * (PREVIEW_POWER_LEVELS - 1));
	if (level >= PREVIEW_POWER_LEVELS) level = PREVIEW_POWER_LEVELS - 1;
	return level;
}

static void project_to_preview(float x, float y,
		float min_x, float min_y, float scale, float off_x, float off_y, int w, int h,
		lv_point_t* out_pt) {
	float px = (x - min_x) * scale + off_x;
	float py = (float)h - ((y - min_y) * scale + off_y);
	if (px < 0.0f) px = 0.0f;
	if (px >= (float)w) px = (float)(w - 1);
	if (py < 0.0f) py = 0.0f;
	if (py >= (float)h) py = (float)(h - 1);
	out_pt->x = (lv_coord_t)px;
	out_pt->y = (lv_coord_t)py;
}

static void dense_add_x_point(int y, int x, uint8_t pwr, int* y_rows_used) {
	if (y < 0 || y >= PREVIEW_Y_BUCKET_MAX) return;
	if (x < 0) x = 0;

	uint8_t cnt = preview_y_span_count[y];
	for (uint8_t i = 0; i < cnt; i++) {
		// Fusionner si voisin (tolérance 1 px)
		if (x >= (int)preview_y_span_min[y][i] - 1 && x <= (int)preview_y_span_max[y][i] + 1) {
			if (x < preview_y_span_min[y][i]) preview_y_span_min[y][i] = (int16_t)x;
			if (x > preview_y_span_max[y][i]) preview_y_span_max[y][i] = (int16_t)x;
			// Moyenner la puissance
			preview_y_span_pwr[y][i] = (preview_y_span_pwr[y][i] + pwr) / 2;
			return;
		}
	}

	if (cnt < PREVIEW_ROW_SPAN_MAX) {
		preview_y_span_min[y][cnt] = (int16_t)x;
		preview_y_span_max[y][cnt] = (int16_t)x;
		preview_y_span_pwr[y][cnt] = pwr;
		preview_y_span_count[y] = cnt + 1;
		if (cnt == 0) (*y_rows_used)++;
		return;
	}

	// Trop de spans sur cette ligne: élargir le span le plus proche sans traverser toute la ligne
	int best = 0;
	int best_dist = 32767;
	for (int i = 0; i < PREVIEW_ROW_SPAN_MAX; i++) {
		int d = 0;
		if (x < preview_y_span_min[y][i]) d = preview_y_span_min[y][i] - x;
		else if (x > preview_y_span_max[y][i]) d = x - preview_y_span_max[y][i];
		if (d < best_dist) { best_dist = d; best = i; }
	}
	if (x < preview_y_span_min[y][best]) preview_y_span_min[y][best] = (int16_t)x;
	if (x > preview_y_span_max[y][best]) preview_y_span_max[y][best] = (int16_t)x;
	preview_y_span_pwr[y][best] = (preview_y_span_pwr[y][best] + pwr) / 2;
}

// Retourne le nombre total de points dans preview_draw_pts, et remplit preview_segs/preview_seg_total
static int build_preview_points(const char* path, int w, int h) {
	preview_debug_msg[0] = '\0';
	preview_seg_total  = 0;
	preview_y_moves_total = 0;

	if (!path || w <= 10 || h <= 10) {
		snprintf(preview_debug_msg, sizeof(preview_debug_msg), "Param invalide");
		return 0;
	}

	const char* p = path;
	while (*p == ' ' || *p == '/') p++;
	char fullpath[136];
	fullpath[0] = '/';
	strncpy(fullpath + 1, p, sizeof(fullpath) - 2);
	fullpath[sizeof(fullpath) - 1] = '\0';

	static SPIClass _preview_spi(HSPI);
	SD.end();
	_preview_spi.begin(14, 12, 13, 15);
	if (!SD.begin(GPIO_NUM_15, _preview_spi)) {
		snprintf(preview_debug_msg, sizeof(preview_debug_msg), "Mount SD echoue");
		return 0;
	}
	File f = SD.open(fullpath, FILE_READ);
	if (!f) {
		snprintf(preview_debug_msg, sizeof(preview_debug_msg), "Open: %s", fullpath);
		SD.end(); return 0;
	}

	// --- Passe 1 : bornes + comptage segments/Y ---
	float cur_x = 0, cur_y = 0;
	float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
	bool has_bounds = false, absolute_mode = true;
	bool laser_enabled = false;
	float modal_s = 0;
	bool in_laser_seg = false;
	int  lc = 0;
	char buf[160];

	while (f.available()) {
		if (pp_abort) {
			f.close();
			SD.end();
			snprintf(preview_debug_msg, sizeof(preview_debug_msg), "Apercu annule");
			return 0;
		}
		int n = f.readBytesUntil('\n', buf, sizeof(buf)-1);
		if (n <= 0) continue;
		buf[n] = '\0';
		if (n > 0 && buf[n-1] == '\r') buf[--n] = '\0';
		if (++lc % 200 == 0) esp_task_wdt_reset();

		char* cmt = strchr(buf, ';'); if (cmt) *cmt = '\0';

		if (strstr(buf,"G90")||strstr(buf,"g90")) absolute_mode = true;
		if (strstr(buf,"G91")||strstr(buf,"g91")) absolute_mode = false;

		for (const char* mp = buf; *mp; mp++) {
			if ((*mp=='M'||*mp=='m') && !isdigit((unsigned char)*(mp+2))) {
				char md = *(mp+1);
				if (md=='3'||md=='4') laser_enabled = true;
				if (md=='5') { laser_enabled = false; modal_s = 0; }
			}
		}
		float sv = modal_s;
		if (parse_axis_value(buf, 'S', &sv)) modal_s = sv;

		int mtype = get_motion_type(buf);
		if (mtype < 0) continue;

		float vx = 0, vy = 0;
		bool hx = parse_axis_value(buf,'X',&vx);
		bool hy = parse_axis_value(buf,'Y',&vy);
		if (!hx && !hy) continue;
		if (hy) preview_y_moves_total++;

		float nx = cur_x, ny = cur_y;
		if (absolute_mode) { if(hx) nx=vx; if(hy) ny=vy; }
		else               { if(hx) nx+=vx; if(hy) ny+=vy; }

		bool laser_on = (mtype==1) && laser_enabled && (modal_s > 0);
		if (laser_on && !in_laser_seg) {
			preview_seg_total++;
			in_laser_seg = true;
		}
		if (!laser_on) in_laser_seg = false;
		if (laser_on) {
			if (modal_s > preview_max_s) preview_max_s = modal_s;
			if (!has_bounds) {
				min_x=max_x=cur_x; min_y=max_y=cur_y; has_bounds=true;
			}
			if(nx<min_x)min_x=nx; if(nx>max_x)max_x=nx;
			if(ny<min_y)min_y=ny; if(ny>max_y)max_y=ny;
			if(cur_x<min_x)min_x=cur_x; if(cur_x>max_x)max_x=cur_x;
			if(cur_y<min_y)min_y=cur_y; if(cur_y>max_y)max_y=cur_y;
		}
		cur_x=nx; cur_y=ny;
	}
	f.close();

	if (!has_bounds || preview_seg_total <= 0) {
		snprintf(preview_debug_msg, sizeof(preview_debug_msg), "Seg:%d Y:%d", preview_seg_total, preview_y_moves_total);
		SD.end();
		return 0;
	}

	// --- Scale ---
	float dx=max_x-min_x, dy=max_y-min_y;
	const int fit_margin = 0;
	float avail_w = (float)(w - 2 * fit_margin);
	float avail_h = (float)(h - 2 * fit_margin);
	if (avail_w < 1.0f) avail_w = (float)w;
	if (avail_h < 1.0f) avail_h = (float)h;

	// Echelle de base: conserver la densite des lignes Y (1 ligne gravee ~= 1 ligne pixel) si possible.
	float scale_from_lines = 1.0f;
	if (dy > 0.001f && preview_y_moves_total > 1) {
		scale_from_lines = ((float)(preview_y_moves_total - 1)) / dy;
		if (scale_from_lines < 1.0f) scale_from_lines = 1.0f;
	}

	// Limites geometriques de la zone d'apercu.
	float scale_cap_w = (dx > 0.001f) ? (avail_w / dx) : 1000.0f;
	float scale_cap_h = (dy > 0.001f) ? (avail_h / dy) : 1000.0f;
	float scale_cap = (scale_cap_w < scale_cap_h) ? scale_cap_w : scale_cap_h;
	if (scale_cap < 0.001f) scale_cap = 0.001f;

	float scale = 1.0f;
	float off_x = 0.0f;
	float off_y = 0.0f;
	scale = (scale_from_lines < scale_cap) ? scale_from_lines : scale_cap;
	off_x = (float)fit_margin + (avail_w - dx * scale) * 0.5f;
	off_y = (float)fit_margin + (avail_h - dy * scale) * 0.5f;
	int scale_permille = (int)(scale * 1000.0f + 0.5f);

	// --- Passe 2 : ne garder qu'un sous-ensemble de segments ---
	if (!SD.begin(GPIO_NUM_15, _preview_spi)) {
		snprintf(preview_debug_msg, sizeof(preview_debug_msg), "Remount SD echoue");
		return 0;
	}
	f = SD.open(fullpath, FILE_READ);
	if (!f) {
		snprintf(preview_debug_msg, sizeof(preview_debug_msg), "Reopen: %s", fullpath);
		SD.end();
		return 0;
	}
	const bool dense_mode = (preview_seg_total > 200) || (preview_y_moves_total > 1000);

	int seg_step = (preview_seg_total > PREVIEW_MAX_SEGS)
		? ((preview_seg_total + PREVIEW_MAX_SEGS - 1) / PREVIEW_MAX_SEGS)
		: 1;

	int y_rows_used = 0;
	if (dense_mode) {
		int hcap = (h < PREVIEW_Y_BUCKET_MAX) ? h : PREVIEW_Y_BUCKET_MAX;
		for (int i = 0; i < hcap; i++) {
			preview_y_span_count[i] = 0;
			for (int k = 0; k < PREVIEW_ROW_SPAN_MAX; k++) {
				preview_y_span_min[i][k] = (int16_t)(w - 1);
				preview_y_span_max[i][k] = 0;
			}
		}
	}

	int out_n = 0;
	int seg_idx = -1;
	int kept_seg_count = 0;
	bool keep_current = false;
	bool in_seg = false;

	cur_x = 0.0f; cur_y = 0.0f;
	absolute_mode = true;
	laser_enabled = false;
	modal_s = 0.0f;
	lc = 0;

	while (f.available()) {
		if (pp_abort) {
			f.close();
			SD.end();
			snprintf(preview_debug_msg, sizeof(preview_debug_msg), "Apercu annule");
			return 0;
		}
		int n = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
		if (n <= 0) continue;
		buf[n] = '\0';
		if (n > 0 && buf[n - 1] == '\r') buf[--n] = '\0';
		if (++lc % 200 == 0) esp_task_wdt_reset();

		char* cmt = strchr(buf, ';');
		if (cmt) *cmt = '\0';

		if (strstr(buf, "G90") || strstr(buf, "g90")) absolute_mode = true;
		if (strstr(buf, "G91") || strstr(buf, "g91")) absolute_mode = false;

		for (const char* mp = buf; *mp; mp++) {
			if ((*mp == 'M' || *mp == 'm') && !isdigit((unsigned char)*(mp + 2))) {
				char md = *(mp + 1);
				if (md == '3' || md == '4') laser_enabled = true;
				if (md == '5') { laser_enabled = false; modal_s = 0.0f; }
			}
		}

		float sv = modal_s;
		if (parse_axis_value(buf, 'S', &sv)) modal_s = sv;

		int mtype = get_motion_type(buf);
		if (mtype < 0) continue;

		float vx = 0.0f, vy = 0.0f;
		bool hx = parse_axis_value(buf, 'X', &vx);
		bool hy = parse_axis_value(buf, 'Y', &vy);
		if (!hx && !hy) continue;

		float nx = cur_x, ny = cur_y;
		if (absolute_mode) {
			if (hx) nx = vx;
			if (hy) ny = vy;
		} else {
			if (hx) nx += vx;
			if (hy) ny += vy;
		}

		bool laser_on = (mtype == 1) && laser_enabled && (modal_s > 0.0f);

		if (laser_on && dense_mode) {
			lv_point_t pt0, pt1;
			project_to_preview(cur_x, cur_y, min_x, min_y, scale, off_x, off_y, w, h, &pt0);
			project_to_preview(nx, ny, min_x, min_y, scale, off_x, off_y, w, h, &pt1);

			uint8_t pwr_level = power_to_level(modal_s, preview_max_s);
			int dxp = (int)pt1.x - (int)pt0.x;
			int dyp = (int)pt1.y - (int)pt0.y;
			int steps = abs(dxp);
			if (abs(dyp) > steps) steps = abs(dyp);
			if (steps < 1) steps = 1;

			for (int s = 0; s <= steps; s++) {
				int xi = (int)pt0.x + (dxp * s) / steps;
				int yi = (int)pt0.y + (dyp * s) / steps;
				if (yi < 0 || yi >= h || yi >= PREVIEW_Y_BUCKET_MAX) continue;
				if (xi >= w) xi = w - 1;
				dense_add_x_point(yi, xi, pwr_level, &y_rows_used);
			}
		}

		if (laser_on && !dense_mode) {
			if (!in_seg) {
				seg_idx++;
				in_seg = true;
				keep_current = ((seg_idx % seg_step) == 0) && (kept_seg_count < PREVIEW_MAX_SEGS);
				if (keep_current) {
					uint8_t pwr_level = power_to_level(modal_s, preview_max_s);
					preview_segs[kept_seg_count].start = (uint16_t)out_n;
					preview_segs[kept_seg_count].count = 0;

					if (out_n + 2 > PREVIEW_DRAW_POINTS_MAX) {
						keep_current = false;
					} else {
						lv_point_t pt0, pt1;
						project_to_preview(cur_x, cur_y, min_x, min_y, scale, off_x, off_y, w, h, &pt0);
						project_to_preview(nx, ny, min_x, min_y, scale, off_x, off_y, w, h, &pt1);
						preview_draw_pts[out_n++] = pt0;
						preview_draw_pts[out_n++] = pt1;
						preview_segs[kept_seg_count].count = (2 | ((uint32_t)pwr_level << 16));
					}
				}
			} else if (keep_current) {
				if (out_n < PREVIEW_DRAW_POINTS_MAX) {
					lv_point_t pt;
					project_to_preview(nx, ny, min_x, min_y, scale, off_x, off_y, w, h, &pt);
					preview_draw_pts[out_n++] = pt;
					preview_segs[kept_seg_count].count++;
				}
			}
		} else if (in_seg) {
			if (keep_current && preview_segs[kept_seg_count].count >= 2) {
				kept_seg_count++;
			}
			in_seg = false;
			keep_current = false;
		}

		cur_x = nx;
		cur_y = ny;
	}

	if (!dense_mode && in_seg && keep_current && kept_seg_count < PREVIEW_MAX_SEGS && preview_segs[kept_seg_count].count >= 2) {
		kept_seg_count++;
	}

	if (dense_mode) {
		int hcap = (h < PREVIEW_Y_BUCKET_MAX) ? h : PREVIEW_Y_BUCKET_MAX;
		int dense_seg_total = 0;
		int dense_rows_total = 0;
		for (int y = 0; y < hcap; y++) {
			bool row_has_span = false;
			for (int k = 0; k < preview_y_span_count[y]; k++) {
				if (preview_y_span_max[y][k] >= preview_y_span_min[y][k]) {
					dense_seg_total++;
					row_has_span = true;
				}
			}
			if (row_has_span) dense_rows_total++;
		}

		out_n = 0;
		kept_seg_count = 0;
		int extra_pool = dense_seg_total - dense_rows_total;
		int extra_capacity = PREVIEW_MAX_SEGS - dense_rows_total;
		int extra_step = 1;
		int extra_idx = 0;
		if (extra_pool > 0 && extra_capacity > 0 && extra_pool > extra_capacity) {
			extra_step = (extra_pool + extra_capacity - 1) / extra_capacity;
		}
		if (extra_capacity < 0) extra_capacity = 0;

		int row_step = 1;
		if (dense_rows_total > PREVIEW_MAX_SEGS) {
			row_step = (dense_rows_total + PREVIEW_MAX_SEGS - 1) / PREVIEW_MAX_SEGS;
		}
		int row_idx = 0;

		for (int y = 0; y < hcap && kept_seg_count < PREVIEW_MAX_SEGS; y++) {
			if (preview_y_span_count[y] == 0) {
				continue;
			}
			// Tri simple des spans par minX pour garder les trous au milieu
			for (int a = 0; a < preview_y_span_count[y] - 1; a++) {
				for (int b = a + 1; b < preview_y_span_count[y]; b++) {
					if (preview_y_span_min[y][b] < preview_y_span_min[y][a]) {
						int16_t tmin = preview_y_span_min[y][a];
						int16_t tmax = preview_y_span_max[y][a];
						uint8_t tpwr = preview_y_span_pwr[y][a];
						preview_y_span_min[y][a] = preview_y_span_min[y][b];
						preview_y_span_max[y][a] = preview_y_span_max[y][b];
						preview_y_span_pwr[y][a] = preview_y_span_pwr[y][b];
						preview_y_span_min[y][b] = tmin;
						preview_y_span_max[y][b] = tmax;
						preview_y_span_pwr[y][b] = tpwr;
					}
				}
			}

			int valid_spans = 0;
			int best_k = -1;
			int best_len = -1;
			for (int k = 0; k < preview_y_span_count[y]; k++) {
				if (preview_y_span_max[y][k] < preview_y_span_min[y][k]) continue;
				valid_spans++;
				int len = (int)preview_y_span_max[y][k] - (int)preview_y_span_min[y][k];
				if (len > best_len) {
					best_len = len;
					best_k = k;
				}
			}
			if (valid_spans == 0 || best_k < 0) continue;

			if ((row_idx % row_step) != 0) {
				row_idx++;
				continue;
			}
			row_idx++;

			if (out_n + 2 > PREVIEW_DRAW_POINTS_MAX) break;
			uint8_t pwr = preview_y_span_pwr[y][best_k];
			if (pwr >= PREVIEW_POWER_LEVELS) pwr = PREVIEW_POWER_LEVELS - 1;
			preview_segs[kept_seg_count].start = (uint16_t)out_n;
			preview_draw_pts[out_n++] = { (lv_coord_t)preview_y_span_min[y][best_k], (lv_coord_t)y };
			preview_draw_pts[out_n++] = { (lv_coord_t)preview_y_span_max[y][best_k], (lv_coord_t)y };
			preview_segs[kept_seg_count].count = (2 | ((uint32_t)pwr << 16));
			kept_seg_count++;

			for (int k = 0; k < preview_y_span_count[y] && kept_seg_count < PREVIEW_MAX_SEGS; k++) {
				if (k == best_k) continue;
				if (preview_y_span_max[y][k] < preview_y_span_min[y][k]) continue;
				if (dense_rows_total >= PREVIEW_MAX_SEGS) break;
				if (extra_capacity <= 0) break;
				if ((extra_idx % extra_step) != 0) {
					extra_idx++;
					continue;
				}
				extra_idx++;
				if (out_n + 2 > PREVIEW_DRAW_POINTS_MAX) break;

				uint8_t pwr2 = preview_y_span_pwr[y][k];
				if (pwr2 >= PREVIEW_POWER_LEVELS) pwr2 = PREVIEW_POWER_LEVELS - 1;
				preview_segs[kept_seg_count].start = (uint16_t)out_n;
				preview_draw_pts[out_n++] = { (lv_coord_t)preview_y_span_min[y][k], (lv_coord_t)y };
				preview_draw_pts[out_n++] = { (lv_coord_t)preview_y_span_max[y][k], (lv_coord_t)y };
				preview_segs[kept_seg_count].count = (2 | ((uint32_t)pwr2 << 16));
				kept_seg_count++;
				extra_capacity--;
			}
		}
	}

	f.close();
	SD.end();
	preview_lines_count = kept_seg_count;
	preview_file_w_um = (int)(max_x - min_x);
	preview_file_h_um = (int)(max_y - min_y);
	snprintf(preview_debug_msg, sizeof(preview_debug_msg), "%c %dx%d Seg:%d Y:%d K:%d.%03d", dense_mode ? 'D' : 'V', preview_file_w_um, preview_file_h_um, preview_seg_total, preview_y_moves_total, scale_permille / 1000, scale_permille % 1000);
	return out_n;
}

static void close_preview_popup(lv_obj_t* obj, lv_event_t event) {
	if (event != LV_EVENT_RELEASED) return;
	// Annuler le parseur
	pp_abort = true;
	if (pp_file) { pp_file.close(); }
	if (pp_state != PREVIEW_PS_IDLE) { SD.end(); pp_state = PREVIEW_PS_IDLE; }
	if (preview_poll_task) { lv_task_del(preview_poll_task); preview_poll_task = NULL; }
	preview_bitmap_free();
	if (preview_popup) {
		lv_obj_del(preview_popup);
		preview_popup = NULL;
		preview_area  = NULL;
		preview_wait_label = NULL;
		preview_stat1_label = NULL;
		preview_stat2_label = NULL;
		for (int i=0;i<PREVIEW_MAX_SEGS;i++) preview_lines[i]=NULL;
		preview_lines_count = 0;
	}
}

// ---------- Parseur en tranches : un appel par tick LVGL (80 lignes max) ----------
static void preview_chunked_tick(lv_task_t* task) {
	char buf[160];

	if (pp_abort || !preview_popup || !preview_area) {
		if (pp_file) pp_file.close();
		if (pp_state != PREVIEW_PS_IDLE) { SD.end(); pp_state = PREVIEW_PS_IDLE; }
		lv_task_set_prio(task, LV_TASK_PRIO_OFF);
		return;
	}

	// --- PASSE 1 ---
	if (pp_state == PREVIEW_PS_PASS1) {
		int count = 0;
		while (count < PREVIEW_LINES_PER_TICK && pp_file.available()) {
			int n = pp_file.readBytesUntil('\n', buf, sizeof(buf)-1);
			if (n <= 0) continue;
			buf[n] = '\0'; if (n > 0 && buf[n-1] == '\r') buf[--n] = '\0';
			if (++pp_lc % 500 == 0) esp_task_wdt_reset();
			char* cmt = strchr(buf, ';'); if (cmt) *cmt = '\0';
			if (strstr(buf,"G90")||strstr(buf,"g90")) pp_absolute=true;
			if (strstr(buf,"G91")||strstr(buf,"g91")) pp_absolute=false;
			for (const char* mp=buf; *mp; mp++) {
				if ((*mp=='M'||*mp=='m') && !isdigit((unsigned char)*(mp+2))) {
					char md=*(mp+1);
					if (md=='3'||md=='4') pp_laser_en=true;
					if (md=='5') { pp_laser_en=false; pp_modal_s=0; }
				}
			}
			float sv=pp_modal_s; if (parse_axis_value(buf,'S',&sv)) pp_modal_s=sv;
			int mtype=get_motion_type(buf);
			if (mtype>=0) {
				float vx=0,vy=0;
				bool hx=parse_axis_value(buf,'X',&vx), hy=parse_axis_value(buf,'Y',&vy);
				if (hx||hy) {
					if (hy) preview_y_moves_total++;
					float nx=pp_cur_x,ny=pp_cur_y;
					if (pp_absolute){if(hx)nx=vx;if(hy)ny=vy;} else {if(hx)nx+=vx;if(hy)ny+=vy;}
					bool laser_on=(mtype==1)&&pp_laser_en&&(pp_modal_s>0);
					if (laser_on&&!pp_in_laser_seg){preview_seg_total++;pp_in_laser_seg=true;}
					if (!laser_on) pp_in_laser_seg=false;
					if (laser_on) {
						if (pp_modal_s>preview_max_s) preview_max_s=pp_modal_s;
						if (!pp_has_bounds){pp_min_x=pp_max_x=pp_cur_x;pp_min_y=pp_max_y=pp_cur_y;pp_has_bounds=true;}
						if(nx<pp_min_x)pp_min_x=nx;if(nx>pp_max_x)pp_max_x=nx;
						if(ny<pp_min_y)pp_min_y=ny;if(ny>pp_max_y)pp_max_y=ny;
						if(pp_cur_x<pp_min_x)pp_min_x=pp_cur_x;if(pp_cur_x>pp_max_x)pp_max_x=pp_cur_x;
						if(pp_cur_y<pp_min_y)pp_min_y=pp_cur_y;if(pp_cur_y>pp_max_y)pp_max_y=pp_cur_y;
					}
					pp_cur_x=nx; pp_cur_y=ny;
				}
			}
			count++;
		}
		if (preview_wait_label) {
			char msg[60]; snprintf(msg,sizeof(msg),"Patientez...\nPasse 1/2: %d lignes",pp_lc);
			lv_label_set_text(preview_wait_label,msg);
		}
		if (!pp_file.available()) { pp_file.close(); pp_state=PREVIEW_PS_SCALE; }
		return;
	}

	// --- CALCUL ECHELLE ---
	if (pp_state == PREVIEW_PS_SCALE) {
		if (!pp_has_bounds || preview_seg_total<=0) {
			snprintf(preview_debug_msg,sizeof(preview_debug_msg),"Seg:%d Y:%d",preview_seg_total,preview_y_moves_total);
			SD.end();
			if (preview_wait_label) lv_label_set_text(preview_wait_label, mc_language.preview_err_no_laser);
			pp_state=PREVIEW_PS_IDLE; lv_task_set_prio(task, LV_TASK_PRIO_OFF); return;
		}
		float dx=pp_max_x-pp_min_x, dy=pp_max_y-pp_min_y;
		float avw=(float)pp_w, avh=(float)pp_h;
		float scw=(dx>0.001f)?(avw/dx):1000.0f, sch=(dy>0.001f)?(avh/dy):1000.0f;
		float sc=(scw<sch)?scw:sch; if(sc<0.001f)sc=0.001f;
		pp_scale=sc; // bitmap: remplir la zone (pas de contrainte par nb de lignes Y)
		pp_off_x=(avw-dx*pp_scale)*0.5f; pp_off_y=(avh-dy*pp_scale)*0.5f;
		pp_scale_permille=(int)(pp_scale*1000.0f+0.5f);
		if (preview_max_s <= 0.0f) {
			preview_max_s = 100.0f;
			if (rpm_max) {
				float ms = rpm_max->get();
				if (ms > 0.0f) preview_max_s = ms;
			}
		}
		pp_dense_mode=(preview_seg_total>200)||(preview_y_moves_total>1000);
		pp_seg_step=(preview_seg_total>PREVIEW_MAX_SEGS)?((preview_seg_total+PREVIEW_MAX_SEGS-1)/PREVIEW_MAX_SEGS):1;
		if (pp_dense_mode) {
			int hcap=(pp_h<PREVIEW_Y_BUCKET_MAX)?pp_h:PREVIEW_Y_BUCKET_MAX;
			for(int i=0;i<hcap;i++){preview_y_span_count[i]=0;for(int k=0;k<PREVIEW_ROW_SPAN_MAX;k++){preview_y_span_min[i][k]=(int16_t)(pp_w-1);preview_y_span_max[i][k]=0;}}
		}
		// Remonter la SD et rouvrir le fichier
		static SPIClass _pp_spi(HSPI);
		SD.end(); _pp_spi.begin(14,12,13,15);
		if (!SD.begin(GPIO_NUM_15,_pp_spi)){snprintf(preview_debug_msg,sizeof(preview_debug_msg),"Remount SD echoue");SD.end();if(preview_wait_label)lv_label_set_text(preview_wait_label,mc_language.preview_err_sd);pp_state=PREVIEW_PS_IDLE;lv_task_set_prio(task,LV_TASK_PRIO_OFF);return;}
		pp_file=SD.open(pp_fullpath,FILE_READ);
		if (!pp_file){SD.end();if(preview_wait_label)lv_label_set_text(preview_wait_label,mc_language.preview_err_open);pp_state=PREVIEW_PS_IDLE;lv_task_set_prio(task,LV_TASK_PRIO_OFF);return;}
		pp_out_n=0;pp_seg_idx=-1;pp_kept_n=0;pp_keep_cur=false;pp_in_seg2=false;pp_y_rows_used=0;
		pp_cur_x=0;pp_cur_y=0;pp_absolute=true;pp_laser_en=false;pp_modal_s=0;pp_lc=0;
		pp_state=PREVIEW_PS_PASS2; return;
	}

	// --- PASSE 2 ---
	if (pp_state == PREVIEW_PS_PASS2) {
		int count=0;
		while (count<PREVIEW_LINES_PER_TICK && pp_file.available()) {
			int n=pp_file.readBytesUntil('\n',buf,sizeof(buf)-1);
			if(n<=0)continue;
			buf[n]='\0';if(n>0&&buf[n-1]=='\r')buf[--n]='\0';
			if(++pp_lc%500==0)esp_task_wdt_reset();
			char*cmt=strchr(buf,';');if(cmt)*cmt='\0';
			if(strstr(buf,"G90")||strstr(buf,"g90"))pp_absolute=true;
			if(strstr(buf,"G91")||strstr(buf,"g91"))pp_absolute=false;
			for(const char*mp=buf;*mp;mp++){
				if((*mp=='M'||*mp=='m')&&!isdigit((unsigned char)*(mp+2))){
					char md=*(mp+1);if(md=='3'||md=='4')pp_laser_en=true;if(md=='5'){pp_laser_en=false;pp_modal_s=0.0f;}
				}
			}
			float sv=pp_modal_s;if(parse_axis_value(buf,'S',&sv))pp_modal_s=sv;
			int mtype=get_motion_type(buf);
			if(mtype>=0){
				float vx=0,vy=0;
				bool hx=parse_axis_value(buf,'X',&vx),hy=parse_axis_value(buf,'Y',&vy);
				if(hx||hy){
					float nx=pp_cur_x,ny=pp_cur_y;
					if(pp_absolute){if(hx)nx=vx;if(hy)ny=vy;}else{if(hx)nx+=vx;if(hy)ny+=vy;}
					bool laser_on=(mtype==1)&&pp_laser_en&&(pp_modal_s>0.0f);
					if(laser_on&&pp_dense_mode){
						lv_point_t pt0,pt1;
						project_to_preview(pp_cur_x,pp_cur_y,pp_min_x,pp_min_y,pp_scale,pp_off_x,pp_off_y,pp_w,pp_h,&pt0);
						project_to_preview(nx,ny,pp_min_x,pp_min_y,pp_scale,pp_off_x,pp_off_y,pp_w,pp_h,&pt1);
						uint8_t pwr_level=power_to_level(pp_modal_s,preview_max_s);
						int dxp=(int)pt1.x-(int)pt0.x,dyp=(int)pt1.y-(int)pt0.y;
						int steps=abs(dxp);if(abs(dyp)>steps)steps=abs(dyp);if(steps<1)steps=1;
						for(int s=0;s<=steps;s++){
							int xi=(int)pt0.x+(dxp*s)/steps,yi=(int)pt0.y+(dyp*s)/steps;
							if(yi<0||yi>=pp_h||yi>=PREVIEW_Y_BUCKET_MAX)continue;
							if(xi>=pp_w)xi=pp_w-1;
							dense_add_x_point(yi,xi,pwr_level,&pp_y_rows_used);
						}
					}
					if(laser_on&&!pp_dense_mode){
						if(!pp_in_seg2){
							pp_seg_idx++;pp_in_seg2=true;
							pp_keep_cur=((pp_seg_idx%pp_seg_step)==0)&&(pp_kept_n<PREVIEW_MAX_SEGS);
							if(pp_keep_cur){
								uint8_t pwr_level=power_to_level(pp_modal_s,preview_max_s);
								preview_segs[pp_kept_n].start=(uint16_t)pp_out_n;
								preview_segs[pp_kept_n].count=0;
								if(pp_out_n+2>PREVIEW_DRAW_POINTS_MAX){pp_keep_cur=false;}else{
									lv_point_t pt0,pt1;
									project_to_preview(pp_cur_x,pp_cur_y,pp_min_x,pp_min_y,pp_scale,pp_off_x,pp_off_y,pp_w,pp_h,&pt0);
									project_to_preview(nx,ny,pp_min_x,pp_min_y,pp_scale,pp_off_x,pp_off_y,pp_w,pp_h,&pt1);
									preview_draw_pts[pp_out_n++]=pt0;preview_draw_pts[pp_out_n++]=pt1;
									preview_segs[pp_kept_n].count=(2|((uint32_t)pwr_level<<16));
								}
							}
						}else if(pp_keep_cur){
							lv_point_t pt;
							project_to_preview(nx,ny,pp_min_x,pp_min_y,pp_scale,pp_off_x,pp_off_y,pp_w,pp_h,&pt);
							uint16_t cur_cnt = (uint16_t)(preview_segs[pp_kept_n].count & 0xFFFF);
							if (cur_cnt < PREVIEW_SEG_POINTS_MAX) {
								if(pp_out_n < PREVIEW_DRAW_POINTS_MAX){
									preview_draw_pts[pp_out_n++] = pt;
									preview_segs[pp_kept_n].count = (preview_segs[pp_kept_n].count & 0xFFFF0000UL) | (uint32_t)(cur_cnt + 1);
								}
							} else {
								// Segment deja dense: garder la forme globale en remplaçant le dernier point
								uint16_t st = preview_segs[pp_kept_n].start;
								if (cur_cnt >= 2 && (uint32_t)st + (uint32_t)cur_cnt - 1 < PREVIEW_DRAW_POINTS_MAX) {
									preview_draw_pts[st + cur_cnt - 1] = pt;
								}
							}
						}
					}else if(pp_in_seg2){
						if(pp_keep_cur&&preview_segs[pp_kept_n].count>=2)pp_kept_n++;
						pp_in_seg2=false;pp_keep_cur=false;
					}
					pp_cur_x=nx;pp_cur_y=ny;
				}
			}
			count++;
		}
		if(preview_wait_label){
			char msg[60];snprintf(msg,sizeof(msg),"Patientez...\nPasse 2/2: %d lignes",pp_lc);
			lv_label_set_text(preview_wait_label,msg);
		}
		if(!pp_file.available()){
			if(!pp_dense_mode&&pp_in_seg2&&pp_keep_cur&&pp_kept_n<PREVIEW_MAX_SEGS&&preview_segs[pp_kept_n].count>=2)pp_kept_n++;
			pp_file.close();SD.end();pp_state=PREVIEW_PS_FINALIZE;
		}
		return;
	}

	// --- FINALISATION (dense) ---
	if (pp_state == PREVIEW_PS_FINALIZE) {
		if (pp_dense_mode) {
			int hcap=(pp_h<PREVIEW_Y_BUCKET_MAX)?pp_h:PREVIEW_Y_BUCKET_MAX;
			int dense_seg_total=0,dense_rows_total=0;
			for(int y=0;y<hcap;y++){
				bool row_has_span=false;
				for(int k=0;k<preview_y_span_count[y];k++){if(preview_y_span_max[y][k]>=preview_y_span_min[y][k]){dense_seg_total++;row_has_span=true;}}
				if(row_has_span)dense_rows_total++;
			}
			pp_out_n=0;pp_kept_n=0;
			int extra_pool=dense_seg_total-dense_rows_total;
			int extra_cap=PREVIEW_MAX_SEGS-dense_rows_total;
			int extra_step=1,extra_idx=0;
			if(extra_pool>0&&extra_cap>0&&extra_pool>extra_cap)extra_step=(extra_pool+extra_cap-1)/extra_cap;
			if(extra_cap<0)extra_cap=0;
			int row_step=(dense_rows_total>PREVIEW_MAX_SEGS)?((dense_rows_total+PREVIEW_MAX_SEGS-1)/PREVIEW_MAX_SEGS):1;
			int row_idx=0;
			for(int y=0;y<hcap&&pp_kept_n<PREVIEW_MAX_SEGS;y++){
				if(preview_y_span_count[y]==0)continue;
				for(int a=0;a<preview_y_span_count[y]-1;a++){for(int b=a+1;b<preview_y_span_count[y];b++){if(preview_y_span_min[y][b]<preview_y_span_min[y][a]){int16_t tmin=preview_y_span_min[y][a],tmax=preview_y_span_max[y][a];uint8_t tpwr=preview_y_span_pwr[y][a];preview_y_span_min[y][a]=preview_y_span_min[y][b];preview_y_span_max[y][a]=preview_y_span_max[y][b];preview_y_span_pwr[y][a]=preview_y_span_pwr[y][b];preview_y_span_min[y][b]=tmin;preview_y_span_max[y][b]=tmax;preview_y_span_pwr[y][b]=tpwr;}}}
				int valid_spans=0,best_k=-1,best_len=-1;
				for(int k=0;k<preview_y_span_count[y];k++){if(preview_y_span_max[y][k]<preview_y_span_min[y][k])continue;valid_spans++;int len=(int)preview_y_span_max[y][k]-(int)preview_y_span_min[y][k];if(len>best_len){best_len=len;best_k=k;}}
				if(valid_spans==0||best_k<0)continue;
				if((row_idx%row_step)!=0){row_idx++;continue;}
				row_idx++;
				if(pp_out_n+2>PREVIEW_DRAW_POINTS_MAX)break;
				uint8_t pwr=preview_y_span_pwr[y][best_k];if(pwr>=PREVIEW_POWER_LEVELS)pwr=PREVIEW_POWER_LEVELS-1;
				preview_segs[pp_kept_n].start=(uint16_t)pp_out_n;
				preview_draw_pts[pp_out_n++]={(lv_coord_t)preview_y_span_min[y][best_k],(lv_coord_t)y};
				preview_draw_pts[pp_out_n++]={(lv_coord_t)preview_y_span_max[y][best_k],(lv_coord_t)y};
				preview_segs[pp_kept_n].count=(2|((uint32_t)pwr<<16));pp_kept_n++;
				for(int k=0;k<preview_y_span_count[y]&&pp_kept_n<PREVIEW_MAX_SEGS;k++){
					if(k==best_k)continue;if(preview_y_span_max[y][k]<preview_y_span_min[y][k])continue;
					if(dense_rows_total>=PREVIEW_MAX_SEGS)break;if(extra_cap<=0)break;
					if((extra_idx%extra_step)!=0){extra_idx++;continue;}extra_idx++;
					if(pp_out_n+2>PREVIEW_DRAW_POINTS_MAX)break;
					uint8_t pwr2=preview_y_span_pwr[y][k];if(pwr2>=PREVIEW_POWER_LEVELS)pwr2=PREVIEW_POWER_LEVELS-1;
					preview_segs[pp_kept_n].start=(uint16_t)pp_out_n;
					preview_draw_pts[pp_out_n++]={(lv_coord_t)preview_y_span_min[y][k],(lv_coord_t)y};
					preview_draw_pts[pp_out_n++]={(lv_coord_t)preview_y_span_max[y][k],(lv_coord_t)y};
					preview_segs[pp_kept_n].count=(2|((uint32_t)pwr2<<16));pp_kept_n++;extra_cap--;
				}
			}
		}
		preview_lines_count=pp_kept_n;
		preview_file_w_um=(int)(pp_max_x-pp_min_x);
		preview_file_h_um=(int)(pp_max_y-pp_min_y);
		snprintf(preview_debug_msg,sizeof(preview_debug_msg),"%c %dx%d Seg:%d Y:%d K:%d.%03d",
			pp_dense_mode?'D':'V',preview_file_w_um,preview_file_h_um,
			preview_seg_total,preview_y_moves_total,pp_scale_permille/1000,pp_scale_permille%1000);
		init_power_colors();
		if (!preview_bitmap_alloc(pp_w, pp_h)) {
			if (preview_wait_label) lv_label_set_text(preview_wait_label, mc_language.preview_err_memory);
			pp_state = PREVIEW_PS_IDLE;
			lv_task_set_prio(task, LV_TASK_PRIO_OFF);
			return;
		}
		pp_render_i = 0;
		pp_render_created = 0;
		pp_render_oom = false;
		if (preview_wait_label) {
			char rmsg[64];
			snprintf(rmsg, sizeof(rmsg), "Rendu bitmap... 0/%d", preview_lines_count);
			lv_label_set_text(preview_wait_label, rmsg);
		}
		pp_state=PREVIEW_PS_RENDER; return;
	}

	// --- RENDU LVGL ---
	if (pp_state == PREVIEW_PS_RENDER) {
		if (!preview_popup||!preview_area){pp_state=PREVIEW_PS_IDLE;lv_task_set_prio(task,LV_TASK_PRIO_OFF);return;}

		int seg_budget = PREVIEW_RENDER_SEGS_PER_TICK;
		while (seg_budget-- > 0 && pp_render_i < preview_lines_count && pp_render_created < PREVIEW_MAX_SEGS) {
			const int i = pp_render_i++;
			const uint16_t cnt_raw = (uint16_t)(preview_segs[i].count & 0xFFFF);
			uint8_t pwr_level = (uint8_t)((preview_segs[i].count >> 16) & 0xFF);
			const uint16_t start = preview_segs[i].start;

			if (cnt_raw < 2) continue;
			if (start >= PREVIEW_DRAW_POINTS_MAX) continue;
			if (((uint32_t)start + (uint32_t)cnt_raw) > (uint32_t)pp_out_n) continue;
			if (((uint32_t)start + (uint32_t)cnt_raw) > PREVIEW_DRAW_POINTS_MAX) continue;
			if (pwr_level >= PREVIEW_POWER_LEVELS) pwr_level = PREVIEW_POWER_LEVELS - 1;

			lv_point_t prev = preview_draw_pts[start];
			for (uint16_t j = 1; j < cnt_raw; j++) {
				lv_point_t cur = preview_draw_pts[start + j];
				int x0 = (pp_w > 1) ? ((int)prev.x * (pp_bmp_w - 1)) / (pp_w - 1) : 0;
				int y0 = (pp_h > 1) ? ((int)prev.y * (pp_bmp_h - 1)) / (pp_h - 1) : 0;
				int x1 = (pp_w > 1) ? ((int)cur.x  * (pp_bmp_w - 1)) / (pp_w - 1) : 0;
				int y1 = (pp_h > 1) ? ((int)cur.y  * (pp_bmp_h - 1)) / (pp_h - 1) : 0;
				preview_bitmap_plot_line(x0, y0, x1, y1, pwr_level);
				prev = cur;
			}
			pp_render_created++;
		}

		if (preview_wait_label && ((pp_render_i % 8) == 0 || pp_render_i >= preview_lines_count)) {
			char rmsg[64];
			snprintf(rmsg, sizeof(rmsg), "Rendu bitmap... %d/%d", pp_render_i, preview_lines_count);
			lv_label_set_text(preview_wait_label, rmsg);
		}

		if (!pp_render_oom && pp_render_i < preview_lines_count && pp_render_created < PREVIEW_MAX_SEGS) {
			return;
		}

		if (preview_wait_label){lv_obj_del(preview_wait_label);preview_wait_label=NULL;}
		preview_lines_count = pp_render_created;
		if (preview_lines_count > 0) {
			preview_bitmap_img = lv_img_create(preview_area, NULL);
			if (preview_bitmap_img) {
				lv_img_set_src(preview_bitmap_img, &preview_bitmap_dsc);
				lv_obj_align(preview_bitmap_img, preview_area, LV_ALIGN_CENTER, 0, 0);
			}
		}

		char stat1[80],stat2[96];
		snprintf(stat1,sizeof(stat1),"%s  %dx%d",preview_debug_msg,preview_file_w_um,preview_file_h_um);
		snprintf(stat2,sizeof(stat2),"Seg:%d  Y:%d  Aff:%d  BMP:%dx%d",preview_seg_total,preview_y_moves_total,preview_lines_count,pp_bmp_w,pp_bmp_h);
		preview_stat1_label=lv_label_create(preview_popup,NULL);
		if (preview_stat1_label) {
			lv_label_set_text(preview_stat1_label,stat1);
			lv_obj_set_pos(preview_stat1_label,10,34+pp_h+2);
		}
		preview_stat2_label=lv_label_create(preview_popup,NULL);
		if (preview_stat2_label) {
			lv_label_set_text(preview_stat2_label,stat2);
			lv_obj_set_pos(preview_stat2_label,10,34+pp_h+18);
		}

		if ((preview_lines_count <= 0 || !preview_bitmap_img) && preview_area) {
			lv_obj_t* lbl = lv_label_create(preview_area, NULL);
			if (lbl) {
				lv_label_set_text(lbl, mc_language.preview_err_render);
				lv_obj_align(lbl, preview_area, LV_ALIGN_CENTER, 0, 0);
			}
		}

		pp_state=PREVIEW_PS_IDLE;lv_task_set_prio(task,LV_TASK_PRIO_OFF);
		return;
	}
}

static void show_file_preview_popup(void) {
	if (preview_popup) return;

	const char* path = frame_ctrl.file_name;
	if (!path || path[0] == '\0') {
		mks_draw_common_popup_info_com("Info", "Aucun fichier", " ", preview_info_popup_cb);
		return;
	}

	preview_bitmap_free();

	lv_style_copy(&preview_popup_style, &lv_style_pretty);
	preview_popup_style.body.main_color = LV_COLOR_MAKE(0x1F, 0x23, 0x33);
	preview_popup_style.body.grad_color = LV_COLOR_MAKE(0x1F, 0x23, 0x33);
	preview_popup_style.body.radius = 8;
	preview_popup_style.body.border.color = LV_COLOR_MAKE(0x04, 0x5A, 0x78);
	preview_popup_style.body.border.width = 2;
	preview_popup_style.text.color = LV_COLOR_WHITE;

	// Adapter la fenetre a la resolution reelle de l'ecran pour eviter tout blocage
	int screen_w = lv_obj_get_width(mks_global.mks_src);
	int screen_h = lv_obj_get_height(mks_global.mks_src);
	if (screen_w < 120) screen_w = 480;
	if (screen_h < 120) screen_h = 320;

	const int popup_margin = 0;
	int popup_w = screen_w - popup_margin * 2;
	int popup_h = screen_h - popup_margin * 2;
	if (popup_w < 280) popup_w = 280;
	if (popup_h < 200) popup_h = 200;

	const int area_x = 2;
	const int area_y = 34;
	const int area_bottom_reserved = 38;
	int area_w = popup_w - area_x * 2;
	int preview_h_px = popup_h - area_y - area_bottom_reserved;
	if (area_w < 100) area_w = 100;
	if (preview_h_px < 80) preview_h_px = 80;

	preview_popup = lv_obj_create(mks_global.mks_src, NULL);
	lv_obj_set_size(preview_popup, popup_w, popup_h);
	lv_obj_align(preview_popup, NULL, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style(preview_popup, &preview_popup_style);

	lv_obj_t* title = lv_label_create(preview_popup, NULL);
	lv_label_set_text(title, mc_language.preview_title);
	lv_obj_set_pos(title, 12, 10);

	lv_style_copy(&preview_area_style, &lv_style_plain);
	preview_area_style.body.main_color = LV_COLOR_MAKE(PREVIEW_BG_R, PREVIEW_BG_G, PREVIEW_BG_B);
	preview_area_style.body.grad_color = LV_COLOR_MAKE(PREVIEW_BG_R, PREVIEW_BG_G, PREVIEW_BG_B);
	preview_area_style.body.border.color = LV_COLOR_MAKE(0x50, 0x6A, 0x7A);
	preview_area_style.body.border.width = 1;
	preview_area_style.body.radius = 2;

	preview_area = lv_obj_create(preview_popup, NULL);
	lv_obj_set_size(preview_area, area_w, preview_h_px);
	lv_obj_set_pos(preview_area, area_x, area_y);
	lv_obj_set_style(preview_area, &preview_area_style);

	// Bouton Fermer en haut à droite
	lv_style_copy(&preview_close_rel, &lv_style_btn_rel);
	preview_close_rel.body.main_color = LV_COLOR_MAKE(0x2A, 0x2F, 0x48);
	preview_close_rel.body.grad_color = LV_COLOR_MAKE(0x2A, 0x2F, 0x48);
	preview_close_rel.text.color = LV_COLOR_WHITE;

	lv_style_copy(&preview_close_pr, &lv_style_btn_pr);
	preview_close_pr.body.main_color = LV_COLOR_MAKE(0x04, 0xD9, 0x19);
	preview_close_pr.body.grad_color = LV_COLOR_MAKE(0x04, 0xD9, 0x19);
	preview_close_pr.text.color = LV_COLOR_BLACK;

	lv_obj_t* close_btn = lv_btn_create(preview_popup, NULL);
	lv_obj_set_size(close_btn, 60, 26);
	lv_obj_set_pos(close_btn, popup_w - 60 - 12, 10);
	lv_btn_set_style(close_btn, LV_BTN_STYLE_REL, &preview_close_rel);
	lv_btn_set_style(close_btn, LV_BTN_STYLE_PR, &preview_close_pr);
	lv_obj_set_event_cb(close_btn, close_preview_popup);

	lv_obj_t* close_lbl = lv_label_create(close_btn, NULL);
	lv_label_set_text(close_lbl, mc_language.preview_close);

	for (int i = 0; i < PREVIEW_MAX_SEGS; i++) preview_lines[i] = NULL;
	preview_lines_count = 0;

	static lv_style_t preview_wait_style;
	lv_style_copy(&preview_wait_style, &lv_style_plain);
	preview_wait_style.text.color = LV_COLOR_MAKE(0xFF, 0xFF, 0x40);
	preview_wait_style.text.font  = &lv_font_roboto_16;
	preview_wait_style.body.opa   = LV_OPA_TRANSP;
	preview_wait_label = lv_label_create(preview_area, NULL);
	lv_label_set_style(preview_wait_label, LV_LABEL_STYLE_MAIN, &preview_wait_style);
	lv_label_set_text(preview_wait_label, mc_language.preview_wait);
	lv_obj_align(preview_wait_label, preview_area, LV_ALIGN_CENTER, 0, 0);

	// Initialiser la machine d'etat du parseur
	const char* pp = path; while (*pp==' '||*pp=='/') pp++;
	pp_fullpath[0]='/'; strncpy(pp_fullpath+1,pp,sizeof(pp_fullpath)-2); pp_fullpath[sizeof(pp_fullpath)-1]='\0';
	pp_w=area_w; pp_h=preview_h_px;
	pp_abort=false; pp_lc=0;
	preview_seg_total=0; preview_y_moves_total=0;
	// Use dynamic max S from file for better color spread.
	preview_max_s=0.0f;
	// Etat passe 1
	pp_cur_x=0;pp_cur_y=0;
	pp_min_x=1e9f;pp_max_x=-1e9f;pp_min_y=1e9f;pp_max_y=-1e9f;
	pp_has_bounds=false;pp_absolute=true;pp_laser_en=false;pp_modal_s=0;pp_in_laser_seg=false;

	// Ouvrir le fichier pour la passe 1
	static SPIClass _pp_spi_init(HSPI);
	SD.end(); _pp_spi_init.begin(14,12,13,15);
	if (!SD.begin(GPIO_NUM_15,_pp_spi_init)){
		if(preview_wait_label)lv_label_set_text(preview_wait_label,mc_language.preview_err_sd);
		return;
	}
	pp_file=SD.open(pp_fullpath,FILE_READ);
	if (!pp_file){
		SD.end();
		if(preview_wait_label)lv_label_set_text(preview_wait_label,mc_language.preview_err_open);
		return;
	}

	if (preview_poll_task){lv_task_del(preview_poll_task);preview_poll_task=NULL;}
	pp_state=PREVIEW_PS_PASS1;
	preview_poll_task=lv_task_create(preview_chunked_tick,50,LV_TASK_PRIO_MID,NULL);


}

static void disp_imgbtn(void);
static void disp_imgbtn_1(void);
static void disp_imgbtn_1_del(void);
static void disp_imgbtn_2(void);
static void disp_imgbtn_2_del(void);
static void disp_label(void);
static void disp_btn(void);
static uint8_t get_id(lv_obj_t* obj) {

    if      (obj == move_page.y_n)  			return ID_INF_UP;
    else if (obj == move_page.y_p)     			return ID_INF_DOWN;
    else if (obj == move_page.x_n)				return ID_INF_LEFT;
    else if (obj == move_page.x_p) 				return ID_INF_RIGHT;
	else if (obj == move_page.z_n) 				return ID_INF_Z_UP;
	else if (obj == move_page.z_p) 				return ID_INF_Z_DOWN;
	else if (obj == move_page.xy_home)     		return ID_INF_XY_HOME;
	else if (obj == move_page.z_home)     		return ID_INF_Z_HOME;
	else if (obj == infile_page.btn_cancle)   	return ID_INF_BACK;
	else if (obj == move_page.btn_len)     		return ID_INF_STEP;
	else if (obj == move_page.btn_speed)		return ID_INF_SPEED;
	else if (obj == infile_page.btn_sculpture)	return ID_INF_CARVE; 
	else if (obj == move_page.next)				return ID_INF_L_NEXT;
	else if (obj == move_page.up)				return ID_INF_L_UP;
	else if (obj == move_page.xy_clear)			return ID_INF_XY_POS;
	else if (obj == move_page.z_clear)			return ID_INF_Z_POS;
	else if (obj == move_page.knife)			return ID_INF_KNIFE;
	else if (obj == infile_page.btn_preview)		return ID_INF_PREVIEW;
}

static void event_handler_cave_yes(lv_obj_t* obj, lv_event_t event) {

	if (event == LV_EVENT_RELEASED) {
		mks_ui_page.mks_ui_page = MKS_UI_PAGE_LOADING;
		cavre_popup_del();
		lv_obj_clean(mks_global.mks_src);
		start_print();
	}
}

static void event_handler_cave_no(lv_obj_t* obj, lv_event_t event) {

	if (event == LV_EVENT_RELEASED) {
		cavre_popup_del();
	}
}




static void set_cooling(lv_obj_t* obj, lv_event_t event) {

	if (event == LV_EVENT_RELEASED) {
		CoolantState state;
		state = coolant_get_state();
		if(state.Flood == 1) {
			MKS_GRBL_CMD_SEND("M9\n");
		}
		else{
			MKS_GRBL_CMD_SEND("M8\n");
		}
	}
}

static void event_handler_back(void) {

	mks_ui_page.mks_ui_page = MKS_UI_PAGE_LOADING;
	lv_obj_clean(mks_global.mks_src);
	mks_draw_ready();
}

static void event_handler_sure(void) {
	char temp[128];
	memset(temp, 0, sizeof(temp));
	memcpy(temp, mks_file_list.filename_str[mks_file_list.file_choose], sizeof(temp));
	if(temp[0]=='/') temp[0] = ' ';
	mks_draw_cavre_popup(temp, event_handler_cave_yes, event_handler_cave_no);
}


static void event_handler_com_info(lv_obj_t* obj, lv_event_t event){

	if (event == LV_EVENT_RELEASED) {
		common_popup_com_del();
	}
}


static void event_handler_carve_set(lv_obj_t* obj, lv_event_t event){

	if (event == LV_EVENT_RELEASED) {
		infile_clean_obj(mks_global.mks_src_3);
		mks_draw_freaure();
	}
}

static void event_henadle_pupup_com(lv_obj_t* obj, lv_event_t event) { 

	if (event == LV_EVENT_RELEASED) {
		common_popup_com_del();
	}
}

static void set_xy_pos(lv_obj_t* obj, lv_event_t event) {
	if(event != LV_EVENT_RELEASED) return;
	if(sys.state == State::Idle && mks_get_motor_status() ) {
		MKS_GRBL_CMD_SEND("G92X0Y0\n");
		mks_draw_common_popup_info_com("Info", "Positioning success", " ", event_henadle_pupup_com);
	}else {
		mks_draw_common_popup_info_com("Warning", "Please wait machine stop!", " ", event_henadle_pupup_com);
	}
}

static void set_z_pos(lv_obj_t* obj, lv_event_t event) {
	if(event != LV_EVENT_RELEASED) return;
	if(sys.state == State::Idle && mks_get_motor_status() ) {
		MKS_GRBL_CMD_SEND("G92Z0\n");
		mks_draw_common_popup_info_com("Info", "Positioning success", " ", event_henadle_pupup_com);
	}else {
		mks_draw_common_popup_info_com("Warning", "Please wait machine stop!", " ", event_henadle_pupup_com);
	}
}

static void set_xyz_pos(lv_obj_t* obj, lv_event_t event) {

	if(event == LV_EVENT_RELEASED ) {
		set_click_status(false);

		if(sys.state == State::Idle && mks_get_motor_status() ) {
			MKS_GRBL_CMD_SEND("G92X0Y0Z0\n");
			mks_draw_common_popup_info_com("Info", "Positioning success", " ", event_henadle_pupup_com);
		}else {
			mks_draw_common_popup_info_com("Warning", "Please wait machine stop!", " ", event_henadle_pupup_com);
		}
	}
}

static void set_knife() {

	if(probe_invert->get()) {
		MKS_GRBL_CMD_SEND("G21 G91 G38.2 Z-50 F80\n");
		mks_draw_common_pupup_info("Info", mc_language.dis_probe_set, " ");
		probe_run.status = PROBE_STAR;

	}else {
		set_click_status(true);
		mks_draw_common_popup_info_com("Info", "Setting error!", "Please set $6=1!", event_henadle_pupup_com);
	}
}

static void event_handler_len_set(void){

	if (mks_grbl.move_dis == M_0_1_MM) {
		mks_grbl.move_dis = M_1_MM;
		lv_label_set_text(move_page.label_len, "1mm");
	}else if(mks_grbl.move_dis == M_1_MM) {
		mks_grbl.move_dis = M_10_MM;
		lv_label_set_text(move_page.label_len, "10mm");
	}else if(mks_grbl.move_dis == M_10_MM) {
		mks_grbl.move_dis = M_50_MM;
		lv_label_set_text(move_page.label_len, "50mm");
	}else if(mks_grbl.move_dis == M_50_MM) {
		mks_grbl.move_dis = M_100_MM;
		lv_label_set_text(move_page.label_len, "100mm");
	}else if(mks_grbl.move_dis == M_100_MM) {
		mks_grbl.move_dis = M_0_1_MM;
		lv_label_set_text(move_page.label_len, "0.1mm");
	}
}

static void event_handler_speed(void){

	if(mks_grbl.move_speed == LOW_SPEED) {
		mks_grbl.move_speed = MID_SPEED;
		mks_lv_label_updata(move_page.label_speed, mc_language.speed_mid);
	}else if(mks_grbl.move_speed == MID_SPEED) {
		mks_grbl.move_speed = HIGHT_SPEED;
		mks_lv_label_updata(move_page.label_speed, mc_language.speed_high);
	}else if(mks_grbl.move_speed == HIGHT_SPEED) {
		mks_grbl.move_speed = LOW_SPEED;
		mks_lv_label_updata(move_page.label_speed, mc_language.speed_low);
	}
}


static void event_handler(lv_obj_t* obj, lv_event_t event) {

	uint8_t id = get_id(obj);

	if(event == LV_EVENT_PRESSED) { 

    }
	
	if((event == LV_EVENT_RELEASED) || (event == LV_EVENT_PRESS_LOST)) {

		switch(id) {
			case ID_INF_UP:	move_ctrl('Y', 1); break;
			case ID_INF_DOWN: move_ctrl('Y', 0); break;
			case ID_INF_LEFT: move_ctrl('X', 0); break;
			case ID_INF_RIGHT: move_ctrl('X', 1); break;
			case ID_INF_Z_UP: move_ctrl('Z', 1); break;
			case ID_INF_Z_DOWN: move_ctrl('Z', 0); break;
			// case ID_INF_XY_POS: set_xy_pos(); break;
			// case ID_INF_Z_POS: set_z_pos(); break;
			case ID_INF_BACK: grbl_send(CLIENT_SERIAL, "into back\n"); event_handler_back(); break;
			case ID_INF_STEP: event_handler_len_set(); break;
			case ID_INF_SPEED: event_handler_speed(); break;
			case ID_INF_CARVE: event_handler_sure(); break; 
			case ID_INF_L_NEXT	:	disp_imgbtn_1_del(); disp_imgbtn_2(); break;
			case ID_INF_L_UP	: 	disp_imgbtn_2_del();  disp_imgbtn_1(); break;
			// case ID_INF_XY_POS:	set_xy_pos();		break;
			// case ID_INF_Z_POS :	set_z_pos(); 		break;
			case ID_INF_KNIFE: set_knife(); break;
			case ID_INF_PREVIEW: show_file_preview_popup(); break;
		}
	}
}

void mks_draw_inFile(char *fn) {

	/* 背景层 */
	mks_global.mks_src_1 = lv_obj_create(mks_global.mks_src, NULL);
	lv_obj_set_size(mks_global.mks_src_1, 460, 90);
    lv_obj_set_pos(mks_global.mks_src_1, 10, 10);

	mks_global.mks_src_2 = lv_obj_create(mks_global.mks_src, NULL);
	lv_obj_set_size(mks_global.mks_src_2, 320, 200);
    lv_obj_set_pos(mks_global.mks_src_2, 10, 110);

	mks_global.mks_src_3 = lv_obj_create(mks_global.mks_src, NULL);
	lv_obj_set_size(mks_global.mks_src_3, 130, 200);
    lv_obj_set_pos(mks_global.mks_src_3, 340, 110);

	lv_obj_set_style(mks_global.mks_src_1, &mks_global.mks_src_1_style);
	lv_obj_set_style(mks_global.mks_src_2, &mks_global.mks_src_2_style);
	lv_obj_set_style(mks_global.mks_src_3, &mks_global.mks_src_3_style);

	disp_imgbtn();
	disp_btn();

	//	记录文件名
	memset(frame_ctrl.file_name, 0, sizeof(frame_ctrl.file_name));
	memcpy(frame_ctrl.file_name, fn, 128);

	//	记录文件大小
	frame_ctrl.file_size = mks_file_list.file_size[mks_file_list.file_choose]; 

	// 显示label
	// if(fn[0] == '/') fn[0] = ' ';
	// label_for_infile_name(mks_global.mks_src_1, infile_page.label_file_name, -120, 0, fn);
	
	disp_label();
	
	mks_ui_page.mks_ui_page = MKS_UI_inFile;
}

static void disp_imgbtn(void) {

	move_page.Back = lv_imgbtn_creat_mks(mks_global.mks_src_1, move_page.Back, &png_back_pre, &back, LV_ALIGN_IN_TOP_LEFT, 10, 5 , event_handler);

	disp_imgbtn_1();

	move_page.y_n = lv_imgbtn_creat_mks(mks_global.mks_src_2, move_page.y_n, &png_up_pre, &png_up, LV_ALIGN_IN_TOP_LEFT, 88, 10, event_handler);
    move_page.y_p = lv_imgbtn_creat_mks(mks_global.mks_src_2, move_page.y_p, &png_down_pre, &png_down, LV_ALIGN_IN_TOP_LEFT, 88, 138, event_handler);
    move_page.x_n = lv_imgbtn_creat_mks(mks_global.mks_src_2, move_page.x_n, &png_left_pre, &png_left, LV_ALIGN_IN_TOP_LEFT, 10, 74, event_handler);
    move_page.x_p = lv_imgbtn_creat_mks(mks_global.mks_src_2, move_page.x_p, &png_right_pre, &png_right, LV_ALIGN_IN_TOP_LEFT, 166, 74, event_handler);
	move_page.z_n = lv_imgbtn_creat_mks(mks_global.mks_src_2, move_page.z_n, &png_z_up_pre, &png_z_up, LV_ALIGN_IN_TOP_LEFT, 244, 10, event_handler);
	move_page.z_p = lv_imgbtn_creat_mks(mks_global.mks_src_2, move_page.z_p, &png_z_down_pre, &png_z_down, LV_ALIGN_IN_TOP_LEFT, 244, 138, event_handler);

	move_page.xy_home = lv_imgbtn_creat_mks(mks_global.mks_src_2, move_page.xy_home, &png_xyhome_pre, &png_xyhome, LV_ALIGN_IN_TOP_LEFT, 88, 74, event_handler);
	move_page.z_home = lv_imgbtn_creat_mks(mks_global.mks_src_2, move_page.z_home, &png_z_home_pre, &png_z_home, LV_ALIGN_IN_TOP_LEFT, 244, 74, event_handler);

	// infile_page.btn_sure_print = lv_imgbtn_creat_mks(mks_global.mks_src_1, infile_page.btn_sure_print, &png_infile_cave_pre, &png_infile_cave, LV_ALIGN_IN_LEFT_MID, 370,-15, event_handler);
	infile_page.btn_cancle = lv_imgbtn_creat_mks(mks_global.mks_src_1, infile_page.btn_cancle, &back, &back, LV_ALIGN_IN_LEFT_MID,10, -15 , event_handler);
}

static void disp_up_set(lv_obj_t* obj, lv_event_t event) {

	if(event != LV_EVENT_RELEASED) return;
	disp_imgbtn_2_del();  
	disp_imgbtn_1();
}

static void disp_down_set(lv_obj_t* obj, lv_event_t event) {

	if(event != LV_EVENT_RELEASED) return;
	disp_imgbtn_1_del(); 
	disp_imgbtn_2();
}

static void disp_imgbtn_1(void) {

	move_page.xy_clear = lv_imgbtn_creat_mks(mks_global.mks_src_1, move_page.xy_clear, &png_xyclear_pre, &png_xyclear, LV_ALIGN_IN_TOP_LEFT, 170, 5, set_xy_pos);
	move_page.z_clear = lv_imgbtn_creat_mks(mks_global.mks_src_1, move_page.z_clear, &png_zclear_pre, &png_zclear, LV_ALIGN_IN_TOP_LEFT, 240, 5, set_z_pos);
	move_page.knife = lv_imgbtn_creat_mks(mks_global.mks_src_1, move_page.knife, &png_knife_pre, &png_knife, LV_ALIGN_IN_TOP_LEFT, 310, 5, event_handler);
	move_page.next = lv_imgbtn_creat_mks(mks_global.mks_src_1, move_page.next, &png_l_next_pre, &png_l_next, LV_ALIGN_IN_TOP_LEFT, 380, 5, disp_down_set);

	move_page.label_xy_clear = label_for_imgbtn_name(mks_global.mks_src_1, move_page.label_xy_clear, move_page.xy_clear, 0, 0, mc_language.xy_clear);
	move_page.label_z_clear = label_for_imgbtn_name(mks_global.mks_src_1, move_page.label_z_clear, move_page.z_clear, 0, 0, mc_language.z_clear);
	move_page.label_knife = label_for_imgbtn_name(mks_global.mks_src_1, move_page.label_knife, move_page.knife, 0, 0, mc_language.knife);
	move_page.label_next = label_for_imgbtn_name(mks_global.mks_src_1, move_page.label_next, move_page.next, 0, 0, mc_language.next);
}

static void disp_imgbtn_1_del(void) {
	lv_obj_del(move_page.xy_clear);
	lv_obj_del(move_page.z_clear);
	lv_obj_del(move_page.knife);
	lv_obj_del(move_page.next);

	lv_obj_del(move_page.label_xy_clear);
	lv_obj_del(move_page.label_z_clear);
	lv_obj_del(move_page.label_knife);
	lv_obj_del(move_page.label_next);
}

static void disp_imgbtn_2(void) {
	move_page.up = lv_imgbtn_creat_mks(mks_global.mks_src_1, move_page.up, &png_l_up_pre, &png_l_up, LV_ALIGN_IN_TOP_LEFT, 170, 5, disp_up_set);
	move_page.cooling = lv_imgbtn_creat_mks(mks_global.mks_src_1, move_page.cooling, &png_cooling_pre, &png_cooling, LV_ALIGN_IN_TOP_LEFT, 240, 5, set_cooling);
	move_page.position = lv_imgbtn_creat_mks(mks_global.mks_src_1, move_page.position, &png_position_pre, &png_position, LV_ALIGN_IN_TOP_LEFT, 310, 5, set_xyz_pos);

	move_page.label_cooling = label_for_imgbtn_name(mks_global.mks_src_1, move_page.label_cooling, move_page.cooling, 0, 0, mc_language.cooling);
	move_page.label_position = label_for_imgbtn_name(mks_global.mks_src_1, move_page.label_position, move_page.position, 0, 0, mc_language.position);
	move_page.label_up = label_for_imgbtn_name(mks_global.mks_src_1, move_page.label_up, move_page.up, 0, 0, mc_language.up);
}

static void disp_imgbtn_2_del(void) {
	lv_obj_del(move_page.cooling);
	lv_obj_del(move_page.position);
	lv_obj_del(move_page.up);

	lv_obj_del(move_page.label_cooling);
	lv_obj_del(move_page.label_position);
	lv_obj_del(move_page.label_up);
}

static void disp_btn(void) {

	/* 按键样式 */
	lv_style_copy(&infile_page.btn_color, &lv_style_scr);
    infile_page.btn_color.body.main_color = LV_COLOR_MAKE(0x17, 0x1A, 0x26);
    infile_page.btn_color.body.grad_color = LV_COLOR_MAKE(0x17, 0x1A, 0x26);
    infile_page.btn_color.body.opa = LV_OPA_COVER;//设置背景色完全不透明
    infile_page.btn_color.text.color = LV_COLOR_WHITE;
	infile_page.btn_color.body.radius = 10;

	lv_style_copy(&infile_page.btn_press_color, &lv_style_scr);
    infile_page.btn_press_color.body.main_color = LV_COLOR_MAKE(0x3F, 0x47, 0x66);
    infile_page.btn_press_color.body.grad_color = LV_COLOR_MAKE(0x3F, 0x47, 0x66);
    infile_page.btn_press_color.body.opa = LV_OPA_COVER;//设置背景色完全不透明
    infile_page.btn_press_color.text.color = LV_COLOR_WHITE;
	infile_page.btn_press_color.body.radius = 10;

	move_page.btn_len = mks_lv_btn_set_for_aglin_screen(mks_global.mks_src_3, move_page.btn_len, 110, 52, LV_ALIGN_IN_TOP_LEFT, 10, 10, event_handler);
	move_page.btn_speed = mks_lv_btn_set_for_aglin_screen(mks_global.mks_src_3, move_page.btn_speed, 110, 42, LV_ALIGN_IN_TOP_LEFT, 10, 58, event_handler);
	infile_page.btn_preview = mks_lv_btn_set_for_aglin_screen(mks_global.mks_src_3, infile_page.btn_preview, 110, 42, LV_ALIGN_IN_TOP_LEFT, 10, 106, event_handler);
	infile_page.btn_sculpture = mks_lv_btn_set_for_aglin_screen(mks_global.mks_src_3, infile_page.btn_sculpture, 110, 42, LV_ALIGN_IN_TOP_LEFT, 10, 154, event_handler);		

	lv_btn_set_style(move_page.btn_len, LV_BTN_STYLE_REL, &infile_page.btn_color);
	lv_btn_set_style(move_page.btn_len, LV_BTN_STYLE_PR, &infile_page.btn_press_color);

	lv_btn_set_style(move_page.btn_speed, LV_BTN_STYLE_REL, &infile_page.btn_color);
	lv_btn_set_style(move_page.btn_speed,LV_BTN_STYLE_PR,&infile_page.btn_press_color);

	lv_btn_set_style(infile_page.btn_preview, LV_BTN_STYLE_REL, &infile_page.btn_color);
	lv_btn_set_style(infile_page.btn_preview,LV_BTN_STYLE_PR,&infile_page.btn_press_color);

	lv_btn_set_style(infile_page.btn_sculpture, LV_BTN_STYLE_REL, &infile_page.btn_color);
	lv_btn_set_style(infile_page.btn_sculpture,LV_BTN_STYLE_PR,&infile_page.btn_press_color);
}

static void disp_label(void) {

	label_for_imgbtn_name(mks_global.mks_src_1, move_page.Label_back, move_page.Back, 0, 0, mc_language.back);
	
	move_page.label_xpos = label_for_text(mks_global.mks_src_1, move_page.label_xpos, NULL, 93, 5, LV_ALIGN_IN_TOP_LEFT,  	"X:0");
	move_page.label_ypos = label_for_text(mks_global.mks_src_1, move_page.label_ypos, NULL, 93, 36, LV_ALIGN_IN_TOP_LEFT,	"Y:0");
	move_page.label_zpos = label_for_text(mks_global.mks_src_1, move_page.label_zpos, NULL, 93, 66, LV_ALIGN_IN_TOP_LEFT,  	"Z:0");

	if(mks_grbl.move_dis == M_0_1_MM) {
		move_page.label_len = mks_lvgl_long_sroll_label_with_wight_set_center(move_page.btn_len, move_page.label_len, 0, 0, "0.1mm", 50);
	}else if(mks_grbl.move_dis == M_1_MM) {
		move_page.label_len = mks_lvgl_long_sroll_label_with_wight_set_center(move_page.btn_len, move_page.label_len, 0, 0, "1mm", 50);
	}else if(mks_grbl.move_dis == M_10_MM) {
		move_page.label_len = mks_lvgl_long_sroll_label_with_wight_set_center(move_page.btn_len, move_page.label_len, 0, 0, "10mm", 50);
	}else if(mks_grbl.move_dis == M_50_MM) {
		move_page.label_len = mks_lvgl_long_sroll_label_with_wight_set_center(move_page.btn_len, move_page.label_len, 0, 0, "50mm", 50);
	}else if(mks_grbl.move_dis == M_100_MM) {
		move_page.label_len = mks_lvgl_long_sroll_label_with_wight_set_center(move_page.btn_len, move_page.label_len, 0, 0, "100mm", 60);
	}
	
	if(mks_grbl.move_speed == LOW_SPEED) {
		move_page.label_speed = mks_lvgl_long_sroll_label_with_wight_set_center(move_page.btn_speed, move_page.label_speed, 0, 0, mc_language.speed_low, 100); //l:500, m:1000, h:2000
	}else if(mks_grbl.move_speed == MID_SPEED) {
		move_page.label_speed = mks_lvgl_long_sroll_label_with_wight_set_center(move_page.btn_speed, move_page.label_speed, 0, 0, mc_language.speed_mid, 100);
	}else if(mks_grbl.move_speed == HIGHT_SPEED) {
		move_page.label_speed = mks_lvgl_long_sroll_label_with_wight_set_center(move_page.btn_speed, move_page.label_speed, 0, 0, mc_language.speed_high, 100);
	}	

	infile_page.label_preview = mks_lvgl_long_sroll_label_with_wight_set_center(infile_page.btn_preview, infile_page.label_preview, 0, 0, "Apercu", 100);

	infile_page.label_sculpture = mks_lvgl_long_sroll_label_with_wight_set_center(infile_page.btn_sculpture, infile_page.label_sculpture, 0, 0, mc_language.sculpture, 100);
}

void infile_clean_obj(lv_obj_t *obj_src) {
	lv_obj_clean(obj_src);
}


