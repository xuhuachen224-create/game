#include "Game_2.h"
#include "LCD.h"
#include "Buzzer.h"
#include "gpio.h"
#include "stm32l4xx_hal.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

extern ST7789V2_cfg_t cfg0;
extern Buzzer_cfg_t buzzer_cfg;

// 参数
#define LCD_W 240
#define LCD_H 240
#define LINE_BOTTOM 200
#define LINE_TOP    150
#define MOVE_SPEED  0.8f
#define SCORE_MOVE  15

#define MAX_NOTES 15
#define NOTE_SZ 12
#define LONG_W 12
#define LONG_H 36
#define CHORD_SZ 14

#define INIT_SPEED 3.0f
#define MAX_SPEED 10.0f
#define GEN_FRAMES 40

#define PERFECT 8
#define GOOD 18
#define BUFFER 15

#define FRAME_MS 16
#define DBL_CLICK_MS 400
#define CHORD_SIM_MS 50
#define MAX_LIVES 5

// 颜色 (索引)
#define COL_BLACK  0
#define COL_WHITE  1
#define COL_RED    2
#define COL_GREEN  3
#define COL_BLUE   4
#define COL_YELLOW 6
#define COL_PINK   7
#define COL_CYAN  14

// 按键
#define BT1_PORT GPIOC
#define BT1_PIN  GPIO_PIN_2
#define BT2_PORT GPIOC
#define BT2_PIN  GPIO_PIN_3
#define DEBOUNCE 50

enum { N_BLUE, N_PINK, L_BLUE, L_PINK, CHORD };

typedef struct {
    uint16_t x; float y; uint8_t act, type;
    uint32_t last_click; uint8_t click_cnt;
} Note_t;

typedef struct {
    uint8_t act; uint16_t x, y; uint8_t type, life;
} Effect_t;

static Note_t notes[MAX_NOTES];
static Effect_t eff[24];
static int8_t lives;
static uint32_t score, frame_cnt, streak;
static float speed = INIT_SPEED;
static uint8_t game_over, judge_flash, miss_flash, crit_flash, moving;
static uint32_t last_hit;
static float judge_y = LINE_BOTTOM, move_dir = -1.0f;
static uint32_t bg_cnt;

// 音效
static void play_perf(void) { buzzer_tone(&buzzer_cfg, 784, 60); HAL_Delay(60); buzzer_off(&buzzer_cfg); }
static void play_good(void) { buzzer_tone(&buzzer_cfg, 523, 50); HAL_Delay(50); buzzer_off(&buzzer_cfg); }
static void play_miss(void) { buzzer_tone(&buzzer_cfg, 220,120); HAL_Delay(120); buzzer_off(&buzzer_cfg); }
static void play_crit(void) {
    buzzer_tone(&buzzer_cfg,1047,40); HAL_Delay(40); buzzer_off(&buzzer_cfg);
    HAL_Delay(30); buzzer_tone(&buzzer_cfg,1047,40); HAL_Delay(40); buzzer_off(&buzzer_cfg);
}
static void play_dbl(void) { buzzer_tone(&buzzer_cfg,659,70); HAL_Delay(70); buzzer_off(&buzzer_cfg); }
static void play_chord(void) {
    buzzer_tone(&buzzer_cfg,1318,40); HAL_Delay(40); buzzer_off(&buzzer_cfg);
    HAL_Delay(30); buzzer_tone(&buzzer_cfg,1318,40); HAL_Delay(40); buzzer_off(&buzzer_cfg);
}
static void play_life(void) { buzzer_tone(&buzzer_cfg,880,80); HAL_Delay(80); buzzer_off(&buzzer_cfg); }

// 背景
static void draw_bg(void) {
    for (int i = 0; i < 40; i++) {
        uint16_t x = (i*131 + bg_cnt) % LCD_W;
        uint16_t y = (i*253) % LCD_H;
        if (y < 80 || y > 200) LCD_Set_Pixel(x, y, COL_WHITE);
    }
    LCD_Draw_Circle(20, LCD_H-20, 6, COL_CYAN, 0);
    LCD_Draw_Line(26, LCD_H-20, 40, LCD_H-15, COL_CYAN);
    LCD_Draw_Line(40, LCD_H-15, 40, LCD_H-5, COL_CYAN);
    LCD_Draw_Circle(LCD_W-20, 20, 6, COL_PINK, 0);
    LCD_Draw_Line(LCD_W-26,20, LCD_W-40,25, COL_PINK);
    LCD_Draw_Line(LCD_W-40,25, LCD_W-40,35, COL_PINK);
    for (int i = 0; i < 12; i++) {
        uint16_t x = ((bg_cnt*2 + i*37) % 240);
        uint16_t y = ((bg_cnt + i*53) % 160);
        LCD_Set_Pixel(x, y+20, COL_YELLOW);
    }
}

// 生成音符
static int count_color(int col) {
    int c=0;
    for (int i=0;i<MAX_NOTES;i++) if (notes[i].act && notes[i].type!=CHORD && (notes[i].type%2)==col) c++;
    return c;
}

static void spawn(void) {
    int is_chord = (rand()%5 == 0);
    int type;
    if (is_chord) {
        type = CHORD;
        int cnt=0; for (int i=0;i<MAX_NOTES;i++) if (notes[i].act && notes[i].type==CHORD) cnt++;
        if (cnt>=2) return;
    } else {
        int is_long = (rand()%2 == 0);
        int col = rand()%2;
        type = is_long ? (col?L_PINK:L_BLUE) : (col?N_PINK:N_BLUE);
        if (count_color(type%2) >= 3) return;
        for (int i=0;i<MAX_NOTES;i++) if (notes[i].act && (notes[i].type%2)==(type%2) && notes[i].y < 50) return;
    }
    for (int i=0;i<MAX_NOTES;i++) {
        if (!notes[i].act) {
            int sz = (type==CHORD) ? CHORD_SZ : (type>=2 ? LONG_H : NOTE_SZ);
            notes[i].x = 20 + (rand() % (LCD_W - sz - 20));
            notes[i].y = 0;
            notes[i].type = type;
            notes[i].last_click = 0;
            notes[i].click_cnt = 0;
            notes[i].act = 1;
            return;
        }
    }
}

static void add_eff(uint16_t x, uint16_t y, uint8_t t) {
    for (int i=0;i<24;i++) if (!eff[i].act) { eff[i].act=1; eff[i].x=x; eff[i].y=y; eff[i].type=t; eff[i].life=20; return; }
}

static void init_btns(void) {
    GPIO_InitTypeDef gp={0};
    gp.Pin = BT1_PIN|BT2_PIN; gp.Mode=GPIO_MODE_INPUT; gp.Pull=GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &gp);
}

static void countdown(void) {
    const char *txt[]={"3","2","1","GO!"};
    for (int i=0;i<4;i++) {
        LCD_Fill_Buffer(COL_BLACK); draw_bg();
        LCD_printString(txt[i], LCD_W/2-20, LCD_H/2-10, COL_YELLOW, 3);
        LCD_Refresh(&cfg0);
        buzzer_tone(&buzzer_cfg,880,30); HAL_Delay(30); buzzer_off(&buzzer_cfg);
        HAL_Delay(470);
    }
}

// 绘制音符（带边框）
static void draw_short(uint16_t x, uint16_t y, int is_blue, uint32_t strk) {
    uint8_t col = is_blue ? COL_BLUE : COL_PINK;
    LCD_Draw_Rect(x,y,NOTE_SZ,NOTE_SZ,col,1);
    uint16_t cx=x+NOTE_SZ/2, cy=y+NOTE_SZ/2;
    if (is_blue) LCD_Draw_Circle(cx,cy,3,COL_WHITE,1);
    else { LCD_Draw_Line(cx-4,cy,cx+4,cy,COL_WHITE); LCD_Draw_Line(cx,cy-4,cx,cy+4,COL_WHITE); }
    if (strk>=10) LCD_Draw_Rect(x-2,y-2,NOTE_SZ+4,NOTE_SZ+4,COL_YELLOW,0);
}
static void draw_long(uint16_t x, uint16_t y, int is_blue, uint32_t strk) {
    uint8_t light = is_blue?COL_CYAN:COL_RED, dark=is_blue?COL_BLUE:COL_PINK;
    LCD_Draw_Rect(x,y,LONG_W,LONG_H/2,light,1);
    LCD_Draw_Rect(x,y+LONG_H/2,LONG_W,LONG_H/2,dark,1);
    LCD_Draw_Line(x,y+LONG_H/2,x+LONG_W,y+LONG_H/2,COL_WHITE);
    if (strk>=10) LCD_Draw_Rect(x-2,y-2,LONG_W+4,LONG_H+4,COL_YELLOW,0);
}
static void draw_chord(uint16_t x, uint16_t y, uint32_t strk) {
    LCD_Draw_Rect(x,y,CHORD_SZ,CHORD_SZ,COL_GREEN,1);
    uint16_t cx=x+CHORD_SZ/2, cy=y+CHORD_SZ/2;
    for (int i=0;i<6;i++) {
        float a=i*3.14159f*2/6;
        int x1=cx+(int)(4*cosf(a)), y1=cy+(int)(4*sinf(a));
        int x2=cx+(int)(4*cosf(a+3.14159f/3)), y2=cy+(int)(4*sinf(a+3.14159f/3));
        LCD_Draw_Line(x1,y1,x2,y2,COL_WHITE);
    }
    if (strk>=10) LCD_Draw_Rect(x-2,y-2,CHORD_SZ+4,CHORD_SZ+4,COL_YELLOW,0);
}

// 通用命中处理
static void proc_hit(int perfect, int crit, int base) {
    int add = base;
    if (perfect) {
        if (crit) add *= 2;
        if (streak>=10) add+=2; if (streak>=20) add+=3; if (streak>=30) add+=5;
        streak++; judge_flash=5; last_hit=HAL_GetTick();
    } else {
        streak++; judge_flash=3; last_hit=HAL_GetTick();
    }
    score += add;
}

static void check_life(void) {
    if (streak>0 && streak%20==0 && lives<MAX_LIVES) { lives++; play_life(); add_eff(LCD_W/2,120,5); }
}

MenuState Game2_Run(void) {
    init_btns();
    srand(HAL_GetTick());

    // 欢迎
    LCD_Fill_Buffer(COL_BLACK); draw_bg();
    LCD_Draw_Rect(30,60,180,80,COL_CYAN,0); LCD_Draw_Rect(32,62,176,76,COL_PINK,0);
    LCD_printString("Rhythm",90,75,COL_YELLOW,2); LCD_printString("Dungeon!",80,100,COL_WHITE,2);
    LCD_printString("Press any button",65,190,COL_GREEN,1);
    LCD_Refresh(&cfg0);
    while (1) if ((HAL_GPIO_ReadPin(BT1_PORT,BT1_PIN)==GPIO_PIN_RESET)||(HAL_GPIO_ReadPin(BT2_PORT,BT2_PIN)==GPIO_PIN_RESET)) { HAL_Delay(200); break; } else HAL_Delay(10);

    // 规则
    LCD_Fill_Buffer(COL_BLACK); draw_bg();
    LCD_Draw_Rect(20,10,200,220,COL_CYAN,0);
    LCD_printString("HOW TO PLAY",70,20,COL_GREEN,2);
    LCD_printString("Blue: PC3, Pink: PC2",40,55,COL_WHITE,1);
    LCD_printString("Long notes: double-tap",40,75,COL_YELLOW,1);
    LCD_printString("Green chord: press both",40,95,COL_GREEN,1);
    LCD_printString("20 combo -> +1 life",40,120,COL_RED,1);
    LCD_printString("Score >=15: line moves",35,145,COL_YELLOW,1);
    LCD_printString("Critical (20%) 2x points",35,170,COL_YELLOW,1);
    LCD_printString("Press any button to start",35,210,COL_WHITE,1);
    LCD_Refresh(&cfg0);
    while (1) if ((HAL_GPIO_ReadPin(BT1_PORT,BT1_PIN)==GPIO_PIN_RESET)||(HAL_GPIO_ReadPin(BT2_PORT,BT2_PIN)==GPIO_PIN_RESET)) { HAL_Delay(200); break; } else HAL_Delay(10);

    countdown();

restart:
    lives=3; score=0; speed=INIT_SPEED; frame_cnt=0; streak=0;
    game_over=0; judge_flash=0; miss_flash=0; crit_flash=0; last_hit=0;
    moving=0; judge_y=LINE_BOTTOM; move_dir=-1.0f; bg_cnt=0;
    for (int i=0;i<MAX_NOTES;i++) notes[i].act=0;
    for (int i=0;i<24;i++) eff[i].act=0;
    for (int i=0;i<3;i++) spawn();

    // 按键状态重置
    static uint32_t last1=0,last2=0, last1t=0,last2t=0;
    static uint8_t last1s=0,last2s=0, was1=0,was2=0;
    last1=last2=last1t=last2t=0; last1s=last2s=was1=was2=0;

    while (1) {
        uint32_t now = HAL_GetTick();
        bg_cnt++;

        uint8_t b1 = HAL_GPIO_ReadPin(BT1_PORT,BT1_PIN)==GPIO_PIN_RESET;
        uint8_t b2 = HAL_GPIO_ReadPin(BT2_PORT,BT2_PIN)==GPIO_PIN_RESET;
        uint8_t b1p=0, b2p=0;
        if (b1 && !last1s && now-last1 > DEBOUNCE) { b1p=1; last1=now; }
        if (b2 && !last2s && now-last2 > DEBOUNCE) { b2p=1; last2=now; }
        last1s=b1; last2s=b2;
        if (b1p) { last1t=now; was1=1; }
        if (b2p) { last2t=now; was2=1; }

        uint8_t chord = 0;
        if (was1 && was2 && abs((int)(last1t-last2t)) <= CHORD_SIM_MS) { chord=1; was1=was2=0; }
        if (now - last1t > CHORD_SIM_MS) was1=0;
        if (now - last2t > CHORD_SIM_MS) was2=0;

        // 查找最近音符宏
        #define FIND(t, sz, use_good) do { \
            best = GOOD+BUFFER+1; idx=-1; \
            for (int i=0;i<MAX_NOTES;i++) if (notes[i].act && notes[i].type==t) { \
                float cy = notes[i].y + (sz)/2.0f; float d = cy - judge_y; \
                if (d >= -BUFFER && d < best) { best=d; idx=i; } \
            } \
            if (idx!=-1 && (best<0?-best:best) <= (use_good?GOOD:GOOD)) valid=1; \
        } while(0)

        // 和弦处理
        if (chord && !game_over && now-last_hit>150) {
            int idx=-1, valid=0; float best;
            FIND(CHORD, CHORD_SZ, 0);
            if (valid) {
                float d = (best<0)?-best:best;
                if (d <= PERFECT) {
                    int crit = (rand()%5==0);
                    play_chord(); if (crit) { crit_flash=3; play_crit(); }
                    proc_hit(1, crit, 5);
                    add_eff(notes[idx].x+CHORD_SZ/2, (uint16_t)judge_y-15, crit?3:6);
                    notes[idx].act=0;
                    check_life();
                } else if (d <= GOOD) {
                    play_good(); proc_hit(0,0,3);
                    add_eff(notes[idx].x+CHORD_SZ/2, (uint16_t)judge_y-15, 1);
                    notes[idx].act=0; check_life();
                } else { lives--; streak=0; play_miss(); judge_flash=2; miss_flash=5; last_hit=now; }
            }
        }

        // 短按 (蓝/粉)
        for (int b=0;b<2;b++) {
            uint8_t pressed = (b==0)?b1p:b2p;
            int target = (b==0)?N_PINK:N_BLUE;
            if (!pressed || game_over || now-last_hit<=150) continue;
            int idx=-1, valid=0; float best;
            FIND(target, NOTE_SZ, 0);
            if (valid) {
                float d = (best<0)?-best:best;
                if (d <= PERFECT) {
                    int crit = (rand()%5==0);
                    if (crit) { crit_flash=3; play_crit(); } else play_perf();
                    proc_hit(1, crit, 3);
                    add_eff(notes[idx].x+NOTE_SZ/2, (uint16_t)judge_y-15, crit?3:0);
                } else if (d <= GOOD) {
                    play_good(); proc_hit(0,0,1);
                    add_eff(notes[idx].x+NOTE_SZ/2, (uint16_t)judge_y-15, 1);
                } else { lives--; streak=0; play_miss(); judge_flash=2; miss_flash=5; idx=-1; }
                if (idx!=-1) { notes[idx].act=0; check_life(); }
            }
        }

        // 长条双击
        for (int b=0;b<2;b++) {
            uint8_t pressed = (b==0)?b1p:b2p;
            int target = (b==0)?L_PINK:L_BLUE;
            if (!pressed || game_over) continue;
            int idx=-1, valid=0; float best;
            FIND(target, LONG_H, 1);
            if (valid) {
                if (notes[idx].click_cnt == 0) {
                    notes[idx].click_cnt = 1;
                    notes[idx].last_click = now;
                } else if (notes[idx].click_cnt == 1) {
                    uint32_t elapsed = now - notes[idx].last_click;
                    if (elapsed <= DBL_CLICK_MS) {
                        int add = 5;
                        if (streak>=10) add+=3; if (streak>=20) add+=5; if (streak>=30) add+=8;
                        score += add; streak++; play_dbl();
                        add_eff(notes[idx].x+LONG_W/2, (uint16_t)judge_y-20, 4);
                        judge_flash=5; last_hit=now;
                        notes[idx].act=0; check_life();
                    } else { notes[idx].click_cnt = 1; notes[idx].last_click = now; }
                }
            } else if (idx!=-1) notes[idx].click_cnt = 0;
        }

        // 清除长条双击标记
        for (int i=0;i<MAX_NOTES;i++) if (notes[i].act && (notes[i].type==L_BLUE||notes[i].type==L_PINK)) {
            float cy = notes[i].y+LONG_H/2.0f;
            if ((cy - judge_y) < -BUFFER || (cy - judge_y) > GOOD) notes[i].click_cnt=0;
        }

        // 更新位置
        if (!game_over) {
            for (int i=0;i<MAX_NOTES;i++) {
                if (!notes[i].act) continue;
                notes[i].y += speed;
                int h = (notes[i].type==CHORD)?CHORD_SZ:(notes[i].type>=2?LONG_H:NOTE_SZ);
                if (notes[i].y + h >= LCD_H) {
                    notes[i].act=0; lives--; streak=0; play_miss(); judge_flash=2; miss_flash=5;
                    add_eff(notes[i].x+h/2, (uint16_t)judge_y-15, 2);
                    if (lives<=0) game_over=1;
                }
            }
            if (streak>=8 && streak%8==0) {
                static uint32_t last_sp=0;
                if (last_sp != streak) { last_sp=streak; speed+=0.5f; if (speed>MAX_SPEED) speed=MAX_SPEED; }
            }
            frame_cnt++;
            if (frame_cnt >= GEN_FRAMES) { frame_cnt=0; spawn(); }
        }

        // 判定线移动
        if (score >= SCORE_MOVE) moving=1;
        if (moving && !game_over) {
            judge_y += move_dir * MOVE_SPEED;
            if (judge_y <= LINE_TOP) { judge_y = LINE_TOP; move_dir = 1.0f; }
            else if (judge_y >= LINE_BOTTOM) { judge_y = LINE_BOTTOM; move_dir = -1.0f; }
        } else if (!game_over) judge_y = LINE_BOTTOM;

        // 特效更新
        for (int i=0;i<24;i++) if (eff[i].act && --eff[i].life==0) eff[i].act=0;
        if (judge_flash) judge_flash--; if (miss_flash) miss_flash--; if (crit_flash) crit_flash--;

        // 渲染
        if (miss_flash) LCD_Fill_Buffer(COL_RED);
        else if (crit_flash) LCD_Fill_Buffer(COL_YELLOW);
        else LCD_Fill_Buffer(COL_BLACK);
        draw_bg();

        LCD_printString("♪ Rhythm Dungeon ♪",50,5,COL_WHITE,1);
        if (moving) LCD_printString("Moving Line!",80,220,COL_YELLOW,1);
        else { char tmp[30]; sprintf(tmp,"Score %d to move",SCORE_MOVE); LCD_printString(tmp,60,220,COL_CYAN,1); }

        uint8_t lcol = COL_WHITE;
        if (judge_flash) lcol = (judge_flash%2)?COL_YELLOW:COL_PINK;
        uint16_t jy = (uint16_t)judge_y;
        for (int x=0;x<LCD_W;x+=12) LCD_Draw_Line(x, jy, x+6, jy, lcol);
        if (judge_flash) for (int x=0;x<LCD_W;x+=12) { LCD_Draw_Line(x,jy-2,x+6,jy-2,lcol); LCD_Draw_Line(x,jy+2,x+6,jy+2,lcol); }

        for (int i=0;i<MAX_NOTES;i++) {
            if (!notes[i].act) continue;
            uint8_t t = notes[i].type;
            if (t == CHORD) draw_chord(notes[i].x, (uint16_t)notes[i].y, streak);
            else {
                int isBlue = (t==N_BLUE || t==L_BLUE);
                if (t==N_BLUE || t==N_PINK) draw_short(notes[i].x, (uint16_t)notes[i].y, isBlue, streak);
                else {
                    draw_long(notes[i].x, (uint16_t)notes[i].y, isBlue, streak);
                    if (notes[i].click_cnt == 1) {
                        uint16_t cx = notes[i].x+LONG_W/2, cy = notes[i].y+LONG_H/2;
                        LCD_Draw_Circle(cx, cy, 8, COL_WHITE, 0);
                    }
                }
            }
        }

        // 特效文字
        for (int i=0;i<24;i++) {
            if (!eff[i].act) continue;
            const char *txt=""; uint8_t col=COL_WHITE, sz=1;
            switch(eff[i].type) {
                case 0: txt="PERFECT!"; col=COL_YELLOW; break;
                case 1: txt="GOOD!"; col=COL_GREEN; break;
                case 2: txt="MISS!"; col=COL_RED; sz=2; break;
                case 3: txt="CRITICAL!"; col=COL_YELLOW; sz=2; break;
                case 4: txt="DOUBLE!"; col=COL_CYAN; break;
                case 5: txt="+1 LIFE!"; col=COL_RED; break;
                case 6: txt="CHORD!"; col=COL_GREEN; break;
            }
            uint16_t yoff = eff[i].life/2;
            LCD_printString(txt, eff[i].x-30, eff[i].y-yoff, col, sz);
        }

        char buf[32];
        sprintf(buf,"Score: %lu",score); LCD_printString(buf,10,30,COL_WHITE,1);
        sprintf(buf,"Lives: %d",lives); LCD_printString(buf,10,45,COL_RED,1);
        sprintf(buf,"Speed: %.1f",speed); LCD_printString(buf,10,60,COL_CYAN,1);
        sprintf(buf,"Combo: %lu",streak); LCD_printString(buf,10,75,COL_GREEN,1);
        if (streak>=10) LCD_printString("GOLDEN!",150,45,COL_YELLOW,1);
        if (streak>=20) LCD_printString("SUPER!",150,60,COL_YELLOW,1);
        if (streak>=30) LCD_printString("MEGA!",150,75,COL_YELLOW,1);

        if (game_over) {
            LCD_printString("GAME OVER",70,110,COL_RED,2);
            sprintf(buf,"Final Score: %lu",score); LCD_printString(buf,50,140,COL_WHITE,1);
            LCD_printString("PC2: Retry",50,180,COL_PINK,1);
            LCD_printString("PC3: Menu",55,200,COL_BLUE,1);
            LCD_Refresh(&cfg0);
            while (game_over) {
                uint8_t r = HAL_GPIO_ReadPin(BT1_PORT,BT1_PIN)==GPIO_PIN_RESET;
                uint8_t e = HAL_GPIO_ReadPin(BT2_PORT,BT2_PIN)==GPIO_PIN_RESET;
                if (r) { HAL_Delay(200); goto restart; }
                if (e) { HAL_Delay(200); buzzer_tone(&buzzer_cfg,600,30); HAL_Delay(100); buzzer_off(&buzzer_cfg); return MENU_STATE_HOME; }
                HAL_Delay(10);
            }
        }

        LCD_Refresh(&cfg0);
        uint32_t ft = HAL_GetTick() - now;
        if (ft < FRAME_MS) HAL_Delay(FRAME_MS - ft);
    }
}