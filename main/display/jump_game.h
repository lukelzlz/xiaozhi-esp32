#ifndef JUMP_GAME_H
#define JUMP_GAME_H

#include <lvgl.h>
#include <atomic>

class JumpGame {
public:
    static JumpGame& GetInstance();
    void Start();
    void Stop();
    bool IsRunning();
    void Press();
    void Release();

private:
    JumpGame() = default;

    void Build();
    void NewRound();
    void GameOver();
    int RandInt(int lo, int hi);

    static void Tick(lv_timer_t* t);

    lv_obj_t* scr_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* cur_ = nullptr;
    lv_obj_t* nxt_ = nullptr;
    lv_obj_t* score_lbl_ = nullptr;
    lv_obj_t* bar_bg_ = nullptr;
    lv_obj_t* bar_fill_ = nullptr;
    lv_obj_t* hint_ = nullptr;
    lv_obj_t* over_lbl_ = nullptr;
    lv_obj_t* over_score_ = nullptr;
    lv_timer_t* tick_ = nullptr;

    std::atomic<bool> running_{false};
    std::atomic<bool> pressed_{false};
    std::atomic<bool> released_{false};

    int state_ = 0;
    enum { S_WAIT = 0, S_CHARGE = 1, S_JUMP = 2, S_OVER = 3, S_FALL = 4 };
    int score_n_ = 0;
    int power_ = 0;

    int cur_w_ = 0;
    int nxt_w_ = 0;
    int gap_ = 0;
    int jump_frame_ = 0;
    int jump_total_ = 0;
    int jx0_ = 0;
    int jdx_ = 0;

    static constexpr int W = 240, H = 240;
    static constexpr int CS = 14;
    static constexpr int PH = 8;
    static constexpr int PY = 170;
    static constexpr int CX = 50;
    static constexpr int JH = 75;
    static constexpr int MAXPOW = 100;
    static constexpr int TICK_MS = 33;
    static constexpr int CHARGE_SPEED = 3;
    static constexpr int JUMP_FRAMES = 24;
    static constexpr int FALL_FRAMES = 15;
};

#endif
