#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAVE_FILE "earn_per_sec.save"

typedef enum { SCREEN_INPUT = 0, SCREEN_RESUME, SCREEN_DISPLAY, SCREEN_CONFIRM } GameScreen;

static Color BG     = { 15,  17,  26, 255 };
static Color ACCENT = { 80, 200, 120, 255 };
static Color BRIGHT = { 220, 220, 220, 255 };
static Color DIM    = { 100, 105, 120, 255 };
static Color BOX    = { 28,  32,  45,  255 };

static void fmt_jpy(char *buf, size_t n, double v) {
    char raw[32];
    snprintf(raw, sizeof(raw), "%.0f", v);
    int rlen = (int)strlen(raw);
    int out = 0;
    for (int i = 0; i < rlen && (size_t)(out + 6) < n; i++) {
        if (i > 0 && (rlen - i) % 3 == 0) buf[out++] = ',';
        buf[out++] = raw[i];
    }
    snprintf(buf + out, n - (size_t)out, " JPY");
}

static void save_state(double income, double acc, double elap) {
    FILE *f = fopen(SAVE_FILE, "w");
    if (!f) return;
    fprintf(f, "%.2f %.10f %.6f\n", income, acc, elap);
    fclose(f);
}

static int load_state(double *income, double *acc, double *elap) {
    FILE *f = fopen(SAVE_FILE, "r");
    if (!f) return 0;
    int ok = fscanf(f, "%lf %lf %lf", income, acc, elap) == 3;
    fclose(f);
    return ok;
}

int main(void) {
    const int W = 800, H = 450;
    InitWindow(W, H, "Earnings Per Second");
    SetTargetFPS(60);

    GameScreen screen = SCREEN_INPUT;
    char input[32]    = "";
    int  inputLen     = 0;
    float blink       = 0.0f;

    double annualIncome = 0.0;
    double perSec       = 0.0;
    double accumulated  = 0.0;
    double elapsed      = 0.0;

    float saveTimer   = 0.0f;
    float savedFlash  = 0.0f;  // 保存時に一時表示

    if (load_state(&annualIncome, &accumulated, &elapsed))
        screen = SCREEN_RESUME;  // perSec はユーザーが選択してから計算

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        switch (screen) {
            case SCREEN_RESUME: {
                if (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_ENTER)) {
                    perSec = annualIncome / (365.0 * 24.0 * 3600.0);
                    screen = SCREEN_DISPLAY;
                }
                if (IsKeyPressed(KEY_N)) {
                    remove(SAVE_FILE);
                    annualIncome = 0.0;
                    accumulated  = 0.0;
                    elapsed      = 0.0;
                    screen       = SCREEN_INPUT;
                }
            } break;

            case SCREEN_INPUT: {
                blink = fmodf(blink + dt, 1.0f);

                int key;
                while ((key = GetCharPressed()) > 0) {
                    if (key >= '0' && key <= '9' && inputLen < 15) {
                        input[inputLen++] = (char)key;
                        input[inputLen]   = '\0';
                    }
                }
                if (IsKeyPressed(KEY_BACKSPACE) && inputLen > 0)
                    input[--inputLen] = '\0';

                if (IsKeyPressed(KEY_ENTER) && inputLen > 0) {
                    annualIncome = atof(input);
                    perSec       = annualIncome / (365.0 * 24.0 * 3600.0);
                    accumulated  = 0.0;
                    elapsed      = 0.0;
                    saveTimer    = 0.0f;
                    save_state(annualIncome, accumulated, elapsed);
                    screen       = SCREEN_DISPLAY;
                }
            } break;

            case SCREEN_DISPLAY: {
                elapsed     += dt;
                accumulated += perSec * dt;

                saveTimer += dt;
                if (saveTimer >= 5.0f) {
                    save_state(annualIncome, accumulated, elapsed);
                    saveTimer  = 0.0f;
                    savedFlash = 2.0f;
                }
                if (savedFlash > 0.0f) savedFlash -= dt;

                if (IsKeyPressed(KEY_ENTER))
                    screen = SCREEN_CONFIRM;
            } break;

            case SCREEN_CONFIRM: {
                elapsed     += dt;
                accumulated += perSec * dt;

                if (IsKeyPressed(KEY_Y) || IsKeyPressed(KEY_ENTER)) {
                    save_state(annualIncome, accumulated, elapsed);  // セーブして新セッション
                    screen       = SCREEN_INPUT;
                    inputLen     = 0;
                    input[0]     = '\0';
                    blink        = 0.0f;
                    accumulated  = 0.0;
                    elapsed      = 0.0;
                    annualIncome = 0.0;
                    perSec       = 0.0;
                }
                if (IsKeyPressed(KEY_N)) {
                    remove(SAVE_FILE);                               // 削除して新セッション
                    screen       = SCREEN_INPUT;
                    inputLen     = 0;
                    input[0]     = '\0';
                    blink        = 0.0f;
                    accumulated  = 0.0;
                    elapsed      = 0.0;
                    annualIncome = 0.0;
                    perSec       = 0.0;
                }
                if (IsKeyPressed(KEY_ESCAPE))
                    screen = SCREEN_DISPLAY;
            } break;
        }

        BeginDrawing();
        ClearBackground(BG);

        switch (screen) {
            case SCREEN_RESUME: {
                // セーブデータの概要
                char accStr[64], annStr[64];
                fmt_jpy(accStr, sizeof(accStr), accumulated);
                fmt_jpy(annStr, sizeof(annStr), annualIncome);

                int sh = (int)elapsed / 3600;
                int sm = ((int)elapsed % 3600) / 60;
                int ss = (int)elapsed % 60;
                char timeStr[32];
                snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", sh, sm, ss);

                const char *title = "Continue from last session?";
                DrawText(title, W/2 - MeasureText(title, 24)/2, 60, 24, BRIGHT);

                // セーブデータ表示ボックス
                DrawRectangleRec((Rectangle){180, 110, 440, 160}, BOX);
                DrawRectangleLinesEx((Rectangle){180, 110, 440, 160}, 2, DIM);

                int lx = 210, vx = 440;
                DrawText("Accumulated", lx, 132, 18, DIM);  DrawText(accStr,  vx, 132, 18, ACCENT);
                DrawText("Annual",      lx, 162, 18, DIM);  DrawText(annStr,  vx, 162, 18, BRIGHT);
                DrawText("Elapsed",     lx, 192, 18, DIM);  DrawText(timeStr, vx, 192, 18, BRIGHT);
                DrawText("Auto-saved",  lx, 222, 18, DIM);  DrawText("yes",   vx, 222, 18, BRIGHT);

                // 選択肢
                int cy = 310;
                DrawRectangleRec((Rectangle){160, cy, 180, 50}, (Color){40, 80, 55, 255});
                DrawRectangleLinesEx((Rectangle){160, cy, 180, 50}, 2, ACCENT);
                const char *lblC = "[C] Continue";
                DrawText(lblC, 160 + 90 - MeasureText(lblC, 18)/2, cy + 16, 18, ACCENT);

                DrawRectangleRec((Rectangle){460, cy, 180, 50}, (Color){60, 35, 28, 255});
                DrawRectangleLinesEx((Rectangle){460, cy, 180, 50}, 2, (Color){220, 100, 60, 255});
                const char *lblN = "[N] New Game";
                DrawText(lblN, 460 + 90 - MeasureText(lblN, 18)/2, cy + 16, 18, (Color){220, 100, 60, 255});
            } break;

            case SCREEN_INPUT: {
                DrawText("Annual Income (JPY)", 100, 130, 22, DIM);

                DrawRectangleRec((Rectangle){100, 165, 450, 58}, BOX);
                DrawRectangleLinesEx((Rectangle){100, 165, 450, 58}, 2, ACCENT);

                char disp[64];
                snprintf(disp, sizeof(disp), "%s%s", input, blink < 0.5f ? "|" : " ");
                DrawText(disp, 116, 181, 30, BRIGHT);

                DrawText("Numbers only  --  ENTER to start", 100, 250, 18, DIM);
            } break;

            case SCREEN_CONFIRM:
            case SCREEN_DISPLAY: {
                int h = (int)elapsed / 3600;
                int m = ((int)elapsed % 3600) / 60;
                int s = (int)elapsed % 60;
                char timeStr[16];
                snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", h, m, s);
                DrawText(timeStr, W/2 - MeasureText(timeStr, 26)/2, 28, 26, DIM);

                char accStr[64];
                fmt_jpy(accStr, sizeof(accStr), accumulated);
                DrawText(accStr, W/2 - MeasureText(accStr, 48)/2, H/2 - 60, 48, ACCENT);

                const char *lbl = "accumulated";
                DrawText(lbl, W/2 - MeasureText(lbl, 18)/2, H/2 - 5, 18, DIM);

                DrawLine(60, H/2 + 25, W - 60, H/2 + 25, BOX);

                char annStr[64], psStr[64], pmStr[64], phStr[64];
                fmt_jpy(annStr, sizeof(annStr), annualIncome);
                snprintf(psStr, sizeof(psStr), "%.4f JPY / sec",  perSec);
                snprintf(pmStr, sizeof(pmStr), "%.2f JPY / min",  perSec * 60.0);
                snprintf(phStr, sizeof(phStr), "%.0f JPY / hour", perSec * 3600.0);

                int c1 = 80, c2 = 420;
                DrawText("Annual",   c1, H/2 + 38,  16, DIM);   DrawText(annStr, c2, H/2 + 38,  16, BRIGHT);
                DrawText("Per sec",  c1, H/2 + 62,  16, DIM);   DrawText(psStr,  c2, H/2 + 62,  16, BRIGHT);
                DrawText("Per min",  c1, H/2 + 86,  16, DIM);   DrawText(pmStr,  c2, H/2 + 86,  16, BRIGHT);
                DrawText("Per hour", c1, H/2 + 110, 16, DIM);   DrawText(phStr,  c2, H/2 + 110, 16, BRIGHT);

                // セーブ状態インジケーター
                Color saveColor = savedFlash > 0.0f ? ACCENT : DIM;
                const char *saveMsg = savedFlash > 0.0f ? "saved" : "auto-saving every 5s";
                DrawText(saveMsg, 60, H - 28, 14, saveColor);

                DrawText("ENTER: new session", W - 220, H - 28, 16, DIM);

                if (screen == SCREEN_CONFIRM) {
                    DrawRectangle(0, 0, W, H, (Color){0, 0, 0, 160});

                    int bx = W/2 - 240, by = H/2 - 95, bw = 480, bh = 190;
                    DrawRectangle(bx, by, bw, bh, (Color){22, 25, 38, 255});
                    DrawRectangleLinesEx((Rectangle){bx, by, bw, bh}, 2, (Color){80, 200, 120, 255});

                    const char *msg1 = "Save before starting new session?";
                    DrawText(msg1, W/2 - MeasureText(msg1, 20)/2, by + 20, 20, BRIGHT);

                    DrawLine(bx + 20, by + 54, bx + bw - 20, by + 54, (Color){40, 45, 65, 255});

                    // Y ボタン（セーブして新セッション）
                    DrawRectangleRec((Rectangle){bx + 30, by + 70, 190, 46}, (Color){30, 65, 45, 255});
                    DrawRectangleLinesEx((Rectangle){bx + 30, by + 70, 190, 46}, 2, ACCENT);
                    const char *lblY = "[Y] Save & Reset";
                    DrawText(lblY, bx + 30 + 95 - MeasureText(lblY, 16)/2, by + 85, 16, ACCENT);

                    // N ボタン（セーブせず新セッション）
                    DrawRectangleRec((Rectangle){bx + 145, by + 125, 190, 46}, (Color){65, 35, 25, 255});
                    DrawRectangleLinesEx((Rectangle){bx + 145, by + 125, 190, 46}, 2, (Color){220, 100, 60, 255});
                    const char *lblN = "[N] Reset only";
                    DrawText(lblN, bx + 145 + 95 - MeasureText(lblN, 16)/2, by + 140, 16, (Color){220, 100, 60, 255});

                    // Esc ボタン（キャンセル）
                    DrawRectangleRec((Rectangle){bx + 260, by + 70, 190, 46}, (Color){35, 38, 55, 255});
                    DrawRectangleLinesEx((Rectangle){bx + 260, by + 70, 190, 46}, 2, DIM);
                    const char *lblE = "[Esc] Cancel";
                    DrawText(lblE, bx + 260 + 95 - MeasureText(lblE, 16)/2, by + 85, 16, DIM);
                }
            } break;
        }

        EndDrawing();
    }

    if (screen == SCREEN_DISPLAY || screen == SCREEN_CONFIRM)
        save_state(annualIncome, accumulated, elapsed);

    CloseWindow();
    return 0;
}
