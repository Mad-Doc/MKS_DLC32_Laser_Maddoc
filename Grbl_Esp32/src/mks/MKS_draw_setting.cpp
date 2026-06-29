// MKS_draw_setting.cpp
// Page de configuration des paramètres GRBL accessibles via l'écran tactile.
// Liste scrollable : clic → popup numpad → validation → envoi $N=val\n

#include "MKS_draw_setting.h"
#include "MKS_draw_language.h"
#include "../Settings.h"
#include "../System.h"

// ─── Table des paramètres exposés ─────────────────────────────────────────────
typedef struct {
    const char* id;       // numéro GRBL (ex: "100")
    const char* name;     // libellé affiché
    float       min_val;
    float       max_val;
    uint8_t     is_int;   // 1 = entier, 0 = flottant
} grbl_param_info_t;

static const grbl_param_info_t grbl_params[] = {
    // ── Stepper ───────────────────────────────────────────────
    {"0",   "Pulse step (us)",        3,      1000,   1},
    {"1",   "Inact. moteur (ms)",     0,      255,    1},
    {"2",   "Step invert mask",       0,      255,    1},
    {"3",   "Dir invert mask",        0,      255,    1},
    {"4",   "Step enable invert",     0,      1,      1},
    // ── Limites / capteurs ────────────────────────────────────
    {"5",   "Limits invert",          0,      1,      1},
    {"6",   "Probe invert",           0,      1,      1},
    // ── Rapport ───────────────────────────────────────────────
    {"10",  "Status mask",            0,      3,      1},
    {"11",  "Junction deviation",     0,      10,     0},
    {"12",  "Arc tolerance",          0,      1,      0},
    {"13",  "Report inches",          0,      1,      1},
    // ── Soft/Hard limits ─────────────────────────────────────
    {"20",  "Soft limits",            0,      1,      1},
    {"21",  "Hard limits",            0,      1,      1},
    // ── Homing ────────────────────────────────────────────────
    {"22",  "Homing actif",           0,      1,      1},
    {"23",  "Homing dir mask",        0,      255,    1},
    {"24",  "Homing avance (mm/mn)",  0,      10000,  0},
    {"25",  "Homing rapide (mm/mn)",  0,      10000,  0},
    {"26",  "Homing debounce (ms)",   0,      10000,  0},
    {"27",  "Homing recul (mm)",      0,      1000,   0},
    // ── Spindle / Laser ───────────────────────────────────────
    {"28",  "PWM frequence (Hz)",     0,      100000, 0},
    {"30",  "Laser S max",            0,      100000, 0},
    {"31",  "Laser S min",            0,      100000, 0},
    {"32",  "Mode laser",             0,      1,      1},
    {"34",  "PWM off (%)",            0,      100,    0},
    {"35",  "PWM min (%)",            0,      100,    0},
    {"36",  "PWM max (%)",            0,      100,    0},
    {"38",  "Bip actif",              0,      1,      1},
    {"40",  "Langue (0=EN 1=FR)",     0,      2,      1},
    // ── Axe X ────────────────────────────────────────────────
    {"100", "X Pas (steps/mm)",       1,      100000, 0},
    {"110", "X Vit.max (mm/mn)",      1,      100000, 0},
    {"120", "X Accel. (mm/s2)",       1,      100000, 0},
    {"130", "X Dim. (mm)",            0,      100000, 0},
    {"140", "X Courant run (A)",      0,      20,     0},
    {"150", "X Courant hold (A)",     0,      20,     0},
    {"160", "X Micropas",             0,      256,    1},
    {"170", "X StallGuard",           0,      255,    1},
    // ── Axe Y ────────────────────────────────────────────────
    {"101", "Y Pas (steps/mm)",       1,      100000, 0},
    {"111", "Y Vit.max (mm/mn)",      1,      100000, 0},
    {"121", "Y Accel. (mm/s2)",       1,      100000, 0},
    {"131", "Y Dim. (mm)",            0,      100000, 0},
    {"141", "Y Courant run (A)",      0,      20,     0},
    {"151", "Y Courant hold (A)",     0,      20,     0},
    {"161", "Y Micropas",             0,      256,    1},
    {"171", "Y StallGuard",           0,      255,    1},
    // ── Axe Z ────────────────────────────────────────────────
    {"102", "Z Pas (steps/mm)",       1,      100000, 0},
    {"112", "Z Vit.max (mm/mn)",      1,      100000, 0},
    {"122", "Z Accel. (mm/s2)",       1,      100000, 0},
    {"132", "Z Dim. (mm)",            0,      100000, 0},
    {"142", "Z Courant run (A)",      0,      20,     0},
    {"152", "Z Courant hold (A)",     0,      20,     0},
    {"162", "Z Micropas",             0,      256,    1},
    {"172", "Z StallGuard",           0,      255,    1},
};

static const int GRBL_PARAM_COUNT = (int)(sizeof(grbl_params) / sizeof(grbl_params[0]));

// ─── État du module ────────────────────────────────────────────────────────────
static lv_obj_t* setting_list      = NULL;
static lv_obj_t* setting_popup_src = NULL;
static lv_obj_t* setting_ta        = NULL;
static lv_obj_t* setting_kb        = NULL;
static int        current_param_idx = -1;

// Styles (module-level pour éviter les allocs répétées)
static lv_style_t s_list_bg;
static lv_style_t s_list_btn_rel;
static lv_style_t s_list_btn_pr;
static lv_style_t s_popup_bg;
static lv_style_t s_ta_bg;
static lv_style_t s_back_rel;
static lv_style_t s_back_pr;

// ─── Lecture de la valeur courante via la liste Settings ──────────────────────
static float get_setting_float(const char* grbl_id) {
    for (Setting* s = Setting::List; s; s = s->next()) {
        const char* gn = s->getGrblName();
        if (gn && strcmp(gn, grbl_id) == 0) {
            // Use GRBL-compatible value to keep booleans numeric (0/1) instead of On/Off.
            const char* sv = s->getCompatibleValue();
            if (sv) return (float)atof(sv);
        }
    }
    return 0.0f;
}

// ─── Formate le texte d'un item de liste ──────────────────────────────────────
static void make_item_text(int idx, char* buf, size_t buf_size) {
    const grbl_param_info_t* p = &grbl_params[idx];
    float val = get_setting_float(p->id);
    if (p->is_int) {
        snprintf(buf, buf_size, "$%-3s %-20s %6d", p->id, p->name, (int)val);
    } else {
        snprintf(buf, buf_size, "$%-3s %-20s %9.3f", p->id, p->name, (double)val);
    }
}

// ─── Met à jour l'affichage d'un item après modification ──────────────────────
static void refresh_list_item(int idx) {
    if (!setting_list) return;
    lv_obj_t* btn = lv_list_get_next_btn(setting_list, NULL);
    for (int i = 0; btn && i < idx; i++) {
        btn = lv_list_get_next_btn(setting_list, btn);
    }
    if (!btn) return;
    lv_obj_t* lbl = lv_list_get_btn_label(btn);
    if (!lbl) return;
    char item_buf[64];
    make_item_text(idx, item_buf, sizeof(item_buf));
    lv_label_set_text(lbl, item_buf);
}

// ─── Ferme le popup d'édition ─────────────────────────────────────────────────
static void close_edit_popup(void) {
    if (setting_popup_src) {
        lv_obj_del(setting_popup_src);
        setting_popup_src = NULL;
        setting_ta        = NULL;
        setting_kb        = NULL;
    }
}

// ─── Gestionnaire du clavier numérique ───────────────────────────────────────
static void setting_kb_event_cb(lv_obj_t* kb, lv_event_t event) {
    // Laisse le comportement par défaut traiter la frappe dans le textarea
    lv_kb_def_event_cb(kb, event);

    if (event == LV_EVENT_APPLY) {
        // Utilisateur a pressé OK
        if (current_param_idx < 0 || current_param_idx >= GRBL_PARAM_COUNT) {
            close_edit_popup();
            return;
        }
        const grbl_param_info_t* p = &grbl_params[current_param_idx];
        const char* text = lv_ta_get_text(setting_ta);
        if (!text || text[0] == '\0') {
            close_edit_popup();
            return;
        }
        float val = (float)atof(text);

        // Brider aux limites du paramètre
        if (val < p->min_val) val = p->min_val;
        if (val > p->max_val) val = p->max_val;

        // Appliquer/sauver directement via le moteur de settings
        char val_buf[24];
        if (p->is_int) {
            snprintf(val_buf, sizeof(val_buf), "%d", (int)val);
        } else {
            snprintf(val_buf, sizeof(val_buf), "%.3f", (double)val);
        }
        Error err = do_command_or_setting(p->id, val_buf, WebUI::AuthenticationLevel::LEVEL_ADMIN, NULL);

        int edited_idx = current_param_idx;
        current_param_idx = -1;
        close_edit_popup();
        if (err == Error::Ok) {
            refresh_list_item(edited_idx);
        } else {
            grbl_msg_sendf(CLIENT_ALL, MsgLevel::Info, "Setting $%s failed (%d)", p->id, (int)err);
        }

    } else if (event == LV_EVENT_CANCEL) {
        current_param_idx = -1;
        close_edit_popup();
    }
}

// ─── Ouvre le popup d'édition pour un paramètre ──────────────────────────────
static void open_edit_popup(int idx) {
    if (setting_popup_src) return;  // déjà ouvert
    current_param_idx = idx;
    const grbl_param_info_t* p = &grbl_params[idx];

    // Panneau de fond du popup
    lv_style_copy(&s_popup_bg, &lv_style_pretty);
    s_popup_bg.body.main_color  = LV_COLOR_MAKE(0x1F, 0x23, 0x33);
    s_popup_bg.body.grad_color  = LV_COLOR_MAKE(0x1F, 0x23, 0x33);
    s_popup_bg.text.color       = LV_COLOR_WHITE;
    s_popup_bg.text.font        = &dlc32Font;
    s_popup_bg.body.radius      = 8;
    s_popup_bg.body.border.color = LV_COLOR_MAKE(0x04, 0x5A, 0x78);
    s_popup_bg.body.border.width = 2;

    setting_popup_src = lv_obj_create(mks_global.mks_src, NULL);
    lv_obj_set_size(setting_popup_src, 440, 300);
    lv_obj_align(setting_popup_src, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style(setting_popup_src, &s_popup_bg);

    // Titre : nom du paramètre + plage
    char title[80];
    if (p->is_int) {
        snprintf(title, sizeof(title), "$%s - %s  [%d .. %d]",
                 p->id, p->name, (int)p->min_val, (int)p->max_val);
    } else {
        snprintf(title, sizeof(title), "$%s - %s  [%.4g .. %.4g]",
                 p->id, p->name, (double)p->min_val, (double)p->max_val);
    }
    lv_obj_t* title_lbl = lv_label_create(setting_popup_src, NULL);
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_BREAK);
    lv_obj_set_width(title_lbl, 420);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_pos(title_lbl, 10, 8);
    lv_obj_set_style(title_lbl, &s_popup_bg);

    // Style du textarea
    lv_style_copy(&s_ta_bg, &lv_style_pretty);
    s_ta_bg.body.main_color = LV_COLOR_MAKE(0x0D, 0x0F, 0x1A);
    s_ta_bg.body.grad_color = LV_COLOR_MAKE(0x0D, 0x0F, 0x1A);
    s_ta_bg.text.color      = LV_COLOR_WHITE;
    s_ta_bg.text.font       = &dlc32Font;
    s_ta_bg.body.radius     = 4;

    // Valeur courante
    float cur_val = get_setting_float(p->id);
    char  val_buf[24];
    if (p->is_int) {
        snprintf(val_buf, sizeof(val_buf), "%d", (int)cur_val);
    } else {
        snprintf(val_buf, sizeof(val_buf), "%.3f", (double)cur_val);
    }

    // Clavier numérique (créé avant le textarea pour pouvoir le lier)
    setting_kb = lv_kb_create(setting_popup_src, NULL);
    lv_kb_set_mode(setting_kb, LV_KB_MODE_NUM);
    lv_kb_set_cursor_manage(setting_kb, true);
    lv_obj_set_size(setting_kb, 420, 175);
    lv_obj_set_pos(setting_kb, 10, 115);
    lv_obj_set_event_cb(setting_kb, setting_kb_event_cb);

    // Textarea (valeur éditable)
    setting_ta = lv_ta_create(setting_popup_src, NULL);
    lv_obj_set_size(setting_ta, 420, 50);
    lv_obj_set_pos(setting_ta, 10, 58);
    lv_ta_set_one_line(setting_ta, true);
    lv_ta_set_text(setting_ta, val_buf);
    lv_ta_set_cursor_pos(setting_ta, LV_TA_CURSOR_LAST);
    lv_ta_set_style(setting_ta, LV_TA_STYLE_BG, &s_ta_bg);
    lv_kb_set_ta(setting_kb, setting_ta);
}

// ─── Gestionnaire des boutons de la liste ────────────────────────────────────
static void list_btn_event_cb(lv_obj_t* btn, lv_event_t event) {
    if (event != LV_EVENT_RELEASED) return;
    // Ignorer si l'utilisateur faisait défiler (scroll)
    lv_indev_t* indev = lv_indev_get_act();
    if (indev && lv_indev_is_dragging(indev)) return;
    // Retrouver l'index par parcours de la liste
    lv_obj_t* cursor = lv_list_get_next_btn(setting_list, NULL);
    int idx = 0;
    while (cursor && cursor != btn) {
        cursor = lv_list_get_next_btn(setting_list, cursor);
        idx++;
    }
    if (cursor == btn && idx < GRBL_PARAM_COUNT) {
        open_edit_popup(idx);
    }
}

// ─── Bouton Retour ────────────────────────────────────────────────────────────
static void setting_back_event_cb(lv_obj_t* obj, lv_event_t event) {
    if (event != LV_EVENT_RELEASED) return;
    mks_clear_setting();
    mks_lv_clean_ui();
    mks_ui_page.mks_ui_page = MKS_UI_PAGE_LOADING;
    mks_ui_page.wait_count  = DEFAULT_UI_COUNT;
    mks_draw_tool();
}

// ─── Nettoyage ────────────────────────────────────────────────────────────────
void mks_clear_setting(void) {
    setting_list      = NULL;
    setting_popup_src = NULL;
    setting_ta        = NULL;
    setting_kb        = NULL;
    current_param_idx = -1;
}

// ─── Dessin de la page principale ─────────────────────────────────────────────
void mks_draw_setting(void) {
    mks_ui_page.mks_ui_page = MKS_UI_Settings;
    mks_ui_page.wait_count  = DEFAULT_UI_COUNT;

    // ── Styles ──
    lv_style_copy(&s_back_rel, &lv_style_btn_rel);
    s_back_rel.body.main_color  = LV_COLOR_MAKE(0x2A, 0x2F, 0x48);
    s_back_rel.body.grad_color  = LV_COLOR_MAKE(0x2A, 0x2F, 0x48);
    s_back_rel.text.color       = LV_COLOR_WHITE;
    s_back_rel.text.font        = &dlc32Font;
    s_back_rel.body.radius      = 6;

    lv_style_copy(&s_back_pr, &s_back_rel);
    s_back_pr.body.main_color = LV_COLOR_MAKE(0x04, 0xD9, 0x19);
    s_back_pr.body.grad_color = LV_COLOR_MAKE(0x04, 0xD9, 0x19);
    s_back_pr.text.color      = LV_COLOR_MAKE(0x00, 0x00, 0x00);

    lv_style_copy(&s_list_bg, &lv_style_scr);
    s_list_bg.body.main_color  = LV_COLOR_MAKE(0x13, 0x12, 0x1A);
    s_list_bg.body.grad_color  = LV_COLOR_MAKE(0x13, 0x12, 0x1A);

    lv_style_copy(&s_list_btn_rel, &lv_style_btn_rel);
    s_list_btn_rel.body.main_color   = LV_COLOR_MAKE(0x2A, 0x2F, 0x48);
    s_list_btn_rel.body.grad_color   = LV_COLOR_MAKE(0x2A, 0x2F, 0x48);
    s_list_btn_rel.text.color        = LV_COLOR_WHITE;
    s_list_btn_rel.text.font         = &dlc32Font;
    s_list_btn_rel.body.radius       = 4;
    s_list_btn_rel.body.border.color = LV_COLOR_MAKE(0x04, 0x5A, 0x78);
    s_list_btn_rel.body.border.width = 1;
    s_list_btn_rel.body.padding.top  = 8;
    s_list_btn_rel.body.padding.bottom = 8;

    lv_style_copy(&s_list_btn_pr, &s_list_btn_rel);
    s_list_btn_pr.body.main_color = LV_COLOR_MAKE(0x04, 0xD9, 0x19);
    s_list_btn_pr.body.grad_color = LV_COLOR_MAKE(0x04, 0xD9, 0x19);
    s_list_btn_pr.text.color      = LV_COLOR_MAKE(0x00, 0x00, 0x00);

    // ── En-tête ──
    lv_obj_t* header = lv_obj_create(mks_global.mks_src, NULL);
    lv_obj_set_size(header, 460, 70);
    lv_obj_set_pos(header, 10, 10);
    lv_obj_set_style(header, &mks_global.mks_src_1_style);

    // Titre
    lv_obj_t* title_lbl = lv_label_create(header, NULL);
    lv_label_set_recolor(title_lbl, true);
    lv_label_set_text(title_lbl, mc_language.grbl_settings_title);
    lv_obj_align(title_lbl, header, LV_ALIGN_CENTER, 30, 0);

    // Bouton Retour
    lv_obj_t* back_btn = lv_btn_create(header, NULL);
    lv_obj_set_size(back_btn, 90, 50);
    lv_obj_align(back_btn, header, LV_ALIGN_IN_LEFT_MID, 5, 0);
    lv_btn_set_style(back_btn, LV_BTN_STYLE_REL, &s_back_rel);
    lv_btn_set_style(back_btn, LV_BTN_STYLE_PR,  &s_back_pr);
    lv_obj_set_event_cb(back_btn, setting_back_event_cb);

    lv_obj_t* back_lbl = lv_label_create(back_btn, NULL);
    lv_label_set_text(back_lbl, mc_language.back);

    // ── Liste scrollable ──
    setting_list = lv_list_create(mks_global.mks_src, NULL);
    lv_obj_set_size(setting_list, 460, 228);
    lv_obj_set_pos(setting_list, 10, 88);
    lv_list_set_style(setting_list, LV_LIST_STYLE_BG,      &s_list_bg);
    lv_list_set_style(setting_list, LV_LIST_STYLE_SCRL,    &s_list_bg);
    lv_list_set_style(setting_list, LV_LIST_STYLE_BTN_REL, &s_list_btn_rel);
    lv_list_set_style(setting_list, LV_LIST_STYLE_BTN_PR,  &s_list_btn_pr);
    lv_list_set_sb_mode(setting_list, LV_SB_MODE_ON);

    // Populer la liste
    char item_buf[64];
    for (int i = 0; i < GRBL_PARAM_COUNT; i++) {
        make_item_text(i, item_buf, sizeof(item_buf));
        lv_obj_t* btn = lv_list_add_btn(setting_list, NULL, item_buf);
        lv_obj_set_event_cb(btn, list_btn_event_cb);
    }
}
