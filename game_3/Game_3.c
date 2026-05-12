#include "Game_3.h"
#include "InputHandler.h"
#include "Joystick.h"
#include "Menu.h"
#include "LCD.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* --- 标准颜色索引 --- */
#define COL_BLACK   0
#define COL_WHITE   1
#define COL_RED     2
#define COL_GREEN   3
#define COL_BLUE    4
#define COL_YELLOW  14
#define COL_BG      0

extern ST7789V2_cfg_t cfg0;
extern Joystick_cfg_t joystick_cfg;

typedef enum { E_ALIVE, E_DYING, E_WIN } EntityState;
typedef enum { TYPE_GREEN, TYPE_RED_BOSS, TYPE_KING } MonsterType;

typedef struct { float x, y, vx, vy; int life; int color; } Particle;
typedef struct { float x, y; int active; } Meteor;

typedef struct {
    float x, y;
    int hp, anim_tick, attack_cd, red_killed, green_killed;
    int hurt_t; 
    float move_dir_x;
} GameState_t;

typedef struct {
    float x, y, tx, ty; 
    int hp, max_hp, timer, hurt_t, action;
    MonsterType type;
    EntityState state;
} Monster_t;

static GameState_t g_state;
static Monster_t m_boss;
static Meteor m_meteors[8];
static Particle m_parts[30];

/* --- 粒子特效逻辑 --- */
static void SpawnExplosion(float x, float y, int col, int count) {
    for(int i=0, c=0; i<30 && c<count; i++) {
        if(m_parts[i].life <= 0) {
            m_parts[i] = (Particle){.x=x, .y=y, .life=10+rand()%10, .color=col,
                .vx=(rand()%40-20)/10.0f, .vy=(rand()%40-20)/10.0f};
            c++;
        }
    }
}

/* --- 关卡生成逻辑 --- */
static void Internal_Spawn(MonsterType t) {
    m_boss.type = t; m_boss.state = E_ALIVE; m_boss.hurt_t = 0; m_boss.timer = 0; m_boss.action = 0;
    if (t == TYPE_GREEN) { 
        m_boss.hp = 2; m_boss.max_hp = 2; m_boss.x = 40 + rand()%140; m_boss.y = 70; 
    }
    else if (t == TYPE_RED_BOSS) { 
        m_boss.hp = 8; m_boss.max_hp = 8; m_boss.x = 110; m_boss.y = 80; 
    }
    else { 
        m_boss.hp = 30; m_boss.max_hp = 30; m_boss.x = 100; m_boss.y = 50; 
    }
}

static void Internal_ResetGame(void) {
    g_state.x = 110; g_state.y = 180; g_state.hp = 5;
    g_state.red_killed = 0; g_state.green_killed = 0;
    g_state.anim_tick = 0; g_state.hurt_t = 0; g_state.attack_cd = 0;
    for(int i=0; i<8; i++) m_meteors[i].active = 0;
    for(int i=0; i<30; i++) m_parts[i].life = 0;
    Internal_Spawn(TYPE_GREEN);
}

/* --- 渲染逻辑（包含你要求的国王绘制逻辑） --- */
static void Internal_DrawMonster(Monster_t *m, int tick) {
    if (m->state != E_ALIVE) return;
    
    if (m->type == TYPE_KING) {
        int col = (m->hurt_t > 0) ? COL_WHITE : COL_RED;
        int x = (int)m->x, y = (int)m->y;
        int air_y = (m->action == 2 && m->timer < 12) ? -15 : 0;

        LCD_Draw_Rect(x + 5, y + air_y, 15, 25, col, 1);
        LCD_Draw_Circle(x + 12, y - 5 + air_y, 4, col, 1);
        LCD_Draw_Rect(x + 7, y - 12 + air_y, 10, 5, COL_YELLOW, 1); 
        
        int arm_y_off, sword_x2, sword_y2;
        if (m->action == 2 && m->timer >= 12) { 
            arm_y_off = 25 + air_y; sword_x2 = x + 35; sword_y2 = y + arm_y_off + 20;
        } else if (m->action == 1 || (m->action == 2 && m->timer < 12)) {
            arm_y_off = -15 + air_y; sword_x2 = x + 25; sword_y2 = y + arm_y_off - 20;
        } else {
            arm_y_off = 10 + air_y; sword_x2 = x + 30; sword_y2 = y + arm_y_off - 15;
        }
        LCD_Draw_Line(x + 5, y + 5 + air_y, x - 5, y + 15 + air_y, col); 
        LCD_Draw_Line(x + 20, y + 5 + air_y, x + 25, y + arm_y_off, col); 
        LCD_Draw_Line(x + 25, y + arm_y_off, sword_x2, sword_y2, COL_WHITE); 
        if (m->action == 3) { 
            int r = (tick % 15) + 5; LCD_Draw_Circle(x + 12, y - 25, r, COL_YELLOW, 0); 
        }
        int walk = (m->action == 0) ? (int)(sinf(tick * 0.4f) * 6) : 0;
        LCD_Draw_Line(x + 8, y + 25 + air_y, x + 8 - walk, y + 35 + air_y, col);
        LCD_Draw_Line(x + 16, y + 25 + air_y, x + 16 + walk, y + 35 + air_y, col);
    } else {
        int col = (m->hurt_t > 0) ? COL_WHITE : (m->type == TYPE_GREEN ? COL_GREEN : COL_RED);
        int jh = (int)(fabs(sinf(tick * 0.2f)) * 8);
        int sz = (m->type == TYPE_RED_BOSS) ? 22 : 14;
        LCD_Draw_Rect((int)m->x, (int)m->y - jh, sz, sz-4, col, 1);
        if (m->type == TYPE_RED_BOSS) {
            if (m->timer > 70) LCD_Draw_Circle((int)m->x+11, (int)m->y-jh+9, (tick%6), COL_YELLOW, 0);
            if (m->timer > 0 && m->timer <= 25) LCD_Draw_Line(0, (int)m->y-jh+9, 240, (int)m->y-jh+9, COL_RED);
        }
    }
}

static void Internal_DrawPlayer(int x, int y, int tick, float dx, int atk) {
    int col = (g_state.hurt_t > 0) ? COL_RED : COL_WHITE;
    if (g_state.hurt_t > 0) x += (rand()%4-2);
    LCD_Draw_Circle(x + 7, y + 4, 3, col, 1); 
    LCD_Draw_Line(x + 7, y + 7, x + 7, y + 14, col); 
    int leg = (int)(sinf(tick * 0.5f) * 6);
    LCD_Draw_Line(x + 7, y + 14, x + 7 + leg, y + 20, col);
    LCD_Draw_Line(x + 7, y + 14, x + 7 - leg, y + 20, col);
    int hx = x + 7 + (dx >= 0 ? 8 : -8), hy = y + 10 + (atk > 0 ? 5 : -2);
    LCD_Draw_Line(x + 7, y + 10, hx, hy, col);
    LCD_Draw_Line(hx, hy, hx + (dx >= 0 ? 16 : -16), hy + (atk > 0 ? 15 : -10), COL_YELLOW);
}

MenuState Game3_Run(void) {
    Internal_ResetGame();
    Joystick_t joy; char buf[32];

    while (1) {
        uint32_t f_start = HAL_GetTick();
        Input_Read(); Joystick_Read(&joystick_cfg, &joy);
        if (current_input.btn2_pressed) return MENU_STATE_HOME;

        if (g_state.hp > 0 && m_boss.state != E_WIN) {
            g_state.anim_tick++;
            if (g_state.hurt_t > 0) g_state.hurt_t--;
            if (g_state.attack_cd > 0) g_state.attack_cd--;
            if (m_boss.hurt_t > 0) m_boss.hurt_t--;

            if (fabs(joy.coord.x) > 0.1f) { g_state.x += joy.coord.x * 4.2f; g_state.move_dir_x = joy.coord.x; }
            if (fabs(joy.coord.y) > 0.1f) g_state.y -= joy.coord.y * 4.2f;
            
            if (g_state.x < 5) g_state.x = 5; 
            if (g_state.x > 220) g_state.x = 220;

            for(int i=0; i<30; i++) if(m_parts[i].life > 0) {
                m_parts[i].x += m_parts[i].vx; m_parts[i].y += m_parts[i].vy; m_parts[i].life--;
            }

            if (m_boss.state == E_ALIVE) {
                m_boss.timer++;
                if (m_boss.type == TYPE_KING) {
                    /* --- 国王 Boss 行为逻辑 --- */
                    if (m_boss.action == 0) {
                        if (m_boss.x < g_state.x) m_boss.x += 1.4f; else m_boss.x -= 1.4f;
                        if (m_boss.y < g_state.y) m_boss.y += 0.8f; else m_boss.y -= 0.8f;
                        if (m_boss.timer > 40) { 
                            static int skill_toggle = 1; m_boss.action = (skill_toggle) ? 1 : 3; 
                            skill_toggle = !skill_toggle; m_boss.timer = 0; 
                        }
                    } else if (m_boss.action == 1) { 
                        if (m_boss.timer == 1) { m_boss.tx = g_state.x; m_boss.ty = g_state.y; }
                        if (m_boss.timer > 20) { m_boss.action = 2; m_boss.timer = 0; }
                    } else if (m_boss.action == 2) { 
                        if (m_boss.timer < 12) m_boss.y -= 1.2f;
                        else { m_boss.x += (m_boss.tx - m_boss.x) * 0.45f; m_boss.y += (m_boss.ty - m_boss.y) * 0.45f; }
                        if (fabs(m_boss.y - m_boss.ty) < 10) {
                            if (sqrtf(powf(g_state.x-m_boss.x,2)+powf(g_state.y-m_boss.y,2)) < 35) {
                                if(g_state.hurt_t <= 0) { g_state.hp--; g_state.hurt_t = 20; g_state.y += 40; }
                            }
                            m_boss.action = 0; m_boss.timer = 0; 
                        }
                    } else if (m_boss.action == 3) { 
                        if (m_boss.timer % 6 == 0) {
                            for(int i=0; i<8; i++) if(!m_meteors[i].active) {
                                m_meteors[i].x = rand()%240; m_meteors[i].y = 0; m_meteors[i].active = 1; break;
                            }
                        }
                        if (m_boss.timer > 75) { m_boss.action = 0; m_boss.timer = 0; }
                    }
                } else {
                    /* --- 普通史莱姆逻辑 --- */
                    float spd = (m_boss.type == TYPE_RED_BOSS) ? 1.4f : 2.0f;
                    int is_grounded = ((int)(fabs(sinf(g_state.anim_tick * 0.2f)) * 8) < 3);

                    if (is_grounded) { 
                        if (m_boss.x < g_state.x) m_boss.x += spd; else m_boss.x -= spd;
                        if (m_boss.y < g_state.y) m_boss.y += spd; else m_boss.y -= spd;
                    }

                    // 【碰撞判定：修复无伤害问题】
                    float dist = sqrtf(powf(g_state.x - m_boss.x, 2) + powf(g_state.y - m_boss.y, 2));
                    int hit_r = (m_boss.type == TYPE_RED_BOSS) ? 25 : 18;
                    if (dist < hit_r && g_state.hurt_t <= 0) {
                        g_state.hp--; g_state.hurt_t = 15; // 扣血并进入无敌帧
                        if (g_state.x < m_boss.x) g_state.x -= 15; else g_state.x += 15; // 小击退
                    }

                    if (m_boss.type == TYPE_RED_BOSS) {
                        if (m_boss.timer > 110) m_boss.timer = 0;
                        if (m_boss.timer <= 25 && fabs(g_state.y + 10 - (m_boss.y + 5)) < 15) { 
                            if(g_state.hurt_t <= 0) { g_state.hp--; g_state.hurt_t = 15; }
                        }
                    }
                }

                // 玩家主动攻击检测
                if (current_input.btn3_pressed && g_state.attack_cd <= 0) {
                    g_state.attack_cd = 8;
                    if (sqrtf(powf(g_state.x-m_boss.x,2)+powf(g_state.y-m_boss.y,2)) < 55) {
                        m_boss.hp--; m_boss.hurt_t = 3; SpawnExplosion(m_boss.x, m_boss.y, COL_WHITE, 2);
                        if (m_boss.hp <= 0) {
                            m_boss.state = E_DYING; m_boss.timer = 20;
                            SpawnExplosion(m_boss.x, m_boss.y, COL_RED, 10);
                        }
                    }
                }
            } else if (m_boss.state == E_DYING) {
                m_boss.timer--;
                if (m_boss.timer <= 0) {
                    if (m_boss.type == TYPE_GREEN) {
                        g_state.green_killed++;
                        Internal_Spawn(g_state.green_killed >= 3 ? TYPE_RED_BOSS : TYPE_GREEN);
                    } else if (m_boss.type == TYPE_RED_BOSS) {
                        g_state.red_killed++;
                        Internal_Spawn(g_state.red_killed >= 2 ? TYPE_KING : TYPE_RED_BOSS);
                    } else m_boss.state = E_WIN;
                }
            }

            for(int i=0; i<8; i++) if(m_meteors[i].active) {
                m_meteors[i].y += 7.5f;
                if (sqrtf(powf(g_state.x-m_meteors[i].x,2)+powf(g_state.y-m_meteors[i].y,2)) < 16) { 
                    if(g_state.hurt_t <= 0) { g_state.hp--; g_state.hurt_t = 12; m_meteors[i].active = 0; }
                }
                if (m_meteors[i].y > 240) m_meteors[i].active = 0;
            }
        } else if (current_input.btn3_pressed) Internal_ResetGame();

        /* --- 渲染层 --- */
        LCD_Fill_Buffer(COL_BG);
        for(int i=0; i<30; i++) if(m_parts[i].life > 0) LCD_Draw_Rect(m_parts[i].x, m_parts[i].y, 2, 2, m_parts[i].color, 1);
        
        if (m_boss.state == E_WIN) { LCD_printString("YOU WIN!", 60, 100, COL_YELLOW, 4); }
        else if (g_state.hp <= 0) { LCD_printString("YOU DIED", 60, 100, COL_RED, 4); }
        else {
            if (m_boss.state == E_DYING) { 
                int t = 20 - m_boss.timer;
                LCD_Draw_Rect((int)m_boss.x-t, (int)m_boss.y-t, 5, 5, COL_RED, 1);
                LCD_Draw_Rect((int)m_boss.x+20+t, (int)m_boss.y+t, 5, 5, COL_WHITE, 1);
            } else Internal_DrawMonster(&m_boss, g_state.anim_tick);

            for(int i = 0; i < 8; i++) if(m_meteors[i].active) {
                LCD_Draw_Circle((int)m_meteors[i].x, (int)m_meteors[i].y, 3, COL_YELLOW, 1);
                LCD_Draw_Line((int)m_meteors[i].x, (int)m_meteors[i].y - 3, (int)m_meteors[i].x, (int)m_meteors[i].y - 10, COL_WHITE);
            }
            Internal_DrawPlayer((int)g_state.x, (int)g_state.y, g_state.anim_tick, g_state.move_dir_x, g_state.attack_cd);
            
            // UI 血条
            LCD_Draw_Rect(40, 230, 160, 6, COL_BLACK, 1);
            LCD_Draw_Rect(40, 230, (m_boss.hp * 160 / m_boss.max_hp), 6, COL_RED, 1);
            sprintf(buf, "HP:%d G:%d R:%d", g_state.hp, g_state.green_killed, g_state.red_killed);
            LCD_printString(buf, 5, 5, COL_WHITE, 1);
        }
        
        LCD_Refresh(&cfg0);
        uint32_t wait = HAL_GetTick() - f_start;
        if (wait < 33) HAL_Delay(33 - wait);
    }
}