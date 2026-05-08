#include "jump_game.h"
#include <esp_random.h>
#include <esp_log.h>

static const char *TAG = "JumpGame";

JumpGame& JumpGame::GetInstance() {
    static JumpGame inst;
    return inst;
}

bool JumpGame::IsRunning() { return running_.load(); }

void JumpGame::Press() { pressed_ = true; }
void JumpGame::Release() { released_ = true; }

int JumpGame::RandInt(int lo, int hi) {
    return lo + (esp_random() % (hi - lo + 1));
}

void JumpGame::Build() {
    scr_ = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(scr_);
    lv_obj_set_size(scr_, W, H);
    lv_obj_set_pos(scr_, 0, 0);
    lv_obj_set_style_bg_color(scr_, lv_color_hex(0x0f0f23), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_move_foreground(scr_);

    score_lbl_ = lv_label_create(scr_);
    lv_obj_set_style_text_color(score_lbl_, lv_color_hex(0xffffff), 0);
    lv_obj_align(score_lbl_, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_label_set_text(score_lbl_, "0");

    auto title = lv_label_create(scr_);
    lv_obj_set_style_text_color(title, lv_color_hex(0x555588), 0);
    lv_label_set_text(title, "Jump!");
    lv_obj_align(title, LV_ALIGN_TOP_RIGHT, -12, 10);

    bar_bg_ = lv_obj_create(scr_);
    lv_obj_set_size(bar_bg_, 160, 10);
    lv_obj_set_style_bg_color(bar_bg_, lv_color_hex(0x222244), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar_bg_, 0, 0);
    lv_obj_set_style_radius(bar_bg_, 5, 0);
    lv_obj_align(bar_bg_, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_add_flag(bar_bg_, LV_OBJ_FLAG_HIDDEN);

    bar_fill_ = lv_obj_create(bar_bg_);
    lv_obj_set_size(bar_fill_, 0, 10);
    lv_obj_set_style_bg_color(bar_fill_, lv_color_hex(0x00cc66), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar_fill_, 0, 0);
    lv_obj_set_style_radius(bar_fill_, 5, 0);
    lv_obj_align(bar_fill_, LV_ALIGN_LEFT_MID, 0, 0);

    hint_ = lv_label_create(scr_);
    lv_obj_set_style_text_color(hint_, lv_color_hex(0x666688), 0);
    lv_label_set_text(hint_, "VOL+ \xe8\x93\x84\xe5\x8a\x9b\xe8\xb7\xb3\xe8\xb7\x83");
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, -8);

    cur_ = lv_obj_create(scr_);
    lv_obj_set_style_bg_color(cur_, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_border_width(cur_, 0, 0);
    lv_obj_set_style_radius(cur_, 3, 0);

    nxt_ = lv_obj_create(scr_);
    lv_obj_set_style_bg_color(nxt_, lv_color_hex(0xccccee), LV_PART_MAIN);
    lv_obj_set_style_border_width(nxt_, 0, 0);
    lv_obj_set_style_radius(nxt_, 3, 0);

    body_ = lv_obj_create(scr_);
    lv_obj_set_style_bg_color(body_, lv_color_hex(0xff4444), LV_PART_MAIN);
    lv_obj_set_style_border_width(body_, 0, 0);
    lv_obj_set_style_radius(body_, 4, 0);
    lv_obj_set_size(body_, CS, CS);

    over_lbl_ = lv_label_create(scr_);
    lv_obj_set_style_text_color(over_lbl_, lv_color_hex(0xff6666), 0);
    lv_label_set_text(over_lbl_, "");
    lv_obj_align(over_lbl_, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_flag(over_lbl_, LV_OBJ_FLAG_HIDDEN);

    over_score_ = lv_label_create(scr_);
    lv_obj_set_style_text_color(over_score_, lv_color_hex(0xffffff), 0);
    lv_label_set_text(over_score_, "");
    lv_obj_align(over_score_, LV_ALIGN_CENTER, 0, 15);
    lv_obj_add_flag(over_score_, LV_OBJ_FLAG_HIDDEN);
}

void JumpGame::NewRound() {
    int min_w = 30, max_w = 55;
    int min_gap = 35, max_gap = 70;
    int diff = score_n_ / 5;
    min_w = 30 - diff;
    max_w = 50 - diff;
    min_gap = 35 + diff * 3;
    max_gap = 65 + diff * 3;
    if (min_w < 20) min_w = 20;
    if (max_w < 25) max_w = 25;
    if (max_gap > 90) max_gap = 90;

    cur_w_ = RandInt(min_w, max_w);
    nxt_w_ = RandInt(min_w, max_w);
    gap_ = RandInt(min_gap, max_gap);

    lv_obj_set_size(cur_, cur_w_, PH);
    lv_obj_set_pos(cur_, CX, PY);

    lv_obj_set_size(nxt_, nxt_w_, PH);
    lv_obj_set_pos(nxt_, CX + CS + gap_, PY);

    lv_obj_set_pos(body_, CX, PY - CS);
    lv_obj_remove_flag(body_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_remove_flag(hint_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bar_bg_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(over_lbl_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(over_score_, LV_OBJ_FLAG_HIDDEN);

    state_ = S_WAIT;
    power_ = 0;
}

void JumpGame::Start() {
    if (running_.load()) return;
    running_ = true;
    pressed_ = false;
    released_ = false;
    score_n_ = 0;

    Build();
    NewRound();

    tick_ = lv_timer_create(Tick, TICK_MS, this);
    ESP_LOGI(TAG, "Game started");
}

void JumpGame::Stop() {
    if (!running_.load()) return;
    running_ = false;
    if (tick_) { lv_timer_delete(tick_); tick_ = nullptr; }
    if (scr_) { lv_obj_delete(scr_); scr_ = nullptr; }
    ESP_LOGI(TAG, "Game stopped");
}

void JumpGame::GameOver() {
    state_ = S_OVER;
    lv_obj_add_flag(bar_bg_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text_fmt(over_lbl_, "\xe6\xb8\xb8\xe6\x88\x8f\xe7\xbb\x93\xe6\x9d\x9f");
    lv_obj_remove_flag(over_lbl_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(over_score_, "%s: %d\nVOL- %s  Boot %s",
        "\xe5\xbe\x97\xe5\x88\x86", score_n_,
        "\xe9\x87\x8d\xe6\x9d\xa5", "\xe9\x80\x80\xe5\x87\xba");
    lv_obj_remove_flag(over_score_, LV_OBJ_FLAG_HIDDEN);
}

void JumpGame::Tick(lv_timer_t* t) {
    auto& g = GetInstance();
    if (!g.running_.load()) return;

    bool p = g.pressed_.exchange(false);
    bool r = g.released_.exchange(false);

    switch (g.state_) {
    case S_WAIT:
        if (p) {
            g.state_ = S_CHARGE;
            g.power_ = 0;
            lv_obj_remove_flag(g.bar_bg_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(g.hint_, LV_OBJ_FLAG_HIDDEN);
        }
        break;

    case S_CHARGE:
        g.power_ += CHARGE_SPEED;
        if (g.power_ > MAXPOW) g.power_ = MAXPOW;
        {
            int bw = g.power_ * 160 / MAXPOW;
            lv_obj_set_width(g.bar_fill_, bw);
            if (g.power_ > 70) {
                lv_obj_set_style_bg_color(g.bar_fill_, lv_color_hex(0xff4444), 0);
            } else if (g.power_ > 40) {
                lv_obj_set_style_bg_color(g.bar_fill_, lv_color_hex(0xffaa00), 0);
            } else {
                lv_obj_set_style_bg_color(g.bar_fill_, lv_color_hex(0x00cc66), 0);
            }
        }
        if (r) {
            g.state_ = S_JUMP;
            g.jump_frame_ = 0;
            g.jx0_ = CX;
            g.jdx_ = g.power_ * 11 / 10;
            lv_obj_add_flag(g.bar_bg_, LV_OBJ_FLAG_HIDDEN);
        }
        break;

    case S_JUMP:
        {
            g.jump_frame_++;
            int total = JUMP_FRAMES;
            int t32 = g.jump_frame_ * 1000 / total;
            int progress = t32;
            int x = g.jx0_ + g.jdx_ * progress / 1000;
            int y_norm = 4 * progress * (1000 - progress) / 1000000;
            int y = PY - CS - JH * y_norm;
            lv_obj_set_pos(g.body_, x, y);

            if (g.jump_frame_ >= total) {
                int char_cx = x + CS / 2;
                int plat_l = CX + CS + g.gap_;
                int plat_r = plat_l + g.nxt_w_;
                int tolerance = 5;

                if (char_cx >= plat_l - tolerance && char_cx <= plat_r + tolerance) {
                    g.score_n_++;
                    lv_label_set_text_fmt(g.score_lbl_, "%d", g.score_n_);
                    g.cur_w_ = g.nxt_w_;
                    lv_obj_set_size(g.cur_, g.cur_w_, PH);
                    lv_obj_set_pos(g.cur_, CX, PY);

                    g.nxt_w_ = g.RandInt(25, 50);
                    g.gap_ = g.RandInt(35, 70);
                    lv_obj_set_size(g.nxt_, g.nxt_w_, PH);
                    lv_obj_set_pos(g.nxt_, CX + CS + g.gap_, PY);

                    lv_obj_set_pos(g.body_, CX, PY - CS);
                    g.state_ = S_WAIT;
                    g.power_ = 0;
                    lv_obj_remove_flag(g.hint_, LV_OBJ_FLAG_HIDDEN);
                } else {
                    g.state_ = S_FALL;
                    g.jump_frame_ = 0;
                }
            }
        }
        break;

    case S_OVER:
        if (p) {
            g.score_n_ = 0;
            lv_label_set_text(g.score_lbl_, "0");
            g.NewRound();
        }
        break;

    case S_FALL:
        {
            g.jump_frame_++;
            int x = lv_obj_get_x(g.body_);
            int y = lv_obj_get_y(g.body_);
            y += 8;
            lv_obj_set_pos(g.body_, x, y);
            if (g.jump_frame_ >= FALL_FRAMES || y > H + 20) {
                lv_obj_add_flag(g.body_, LV_OBJ_FLAG_HIDDEN);
                g.GameOver();
            }
        }
        break;
    }
}
