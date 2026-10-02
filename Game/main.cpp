/*******************************************************************************************
*
*   FAIRY SKY GLADE - Micro-Game in C (Raylib + WebAssembly)
*
*   Features:
*     - 1920x1080 Native Resolution
*     - Dynamic progression: platforms spread wider and higher each level
*     - Narrower and faster platforms as levels increase
*     - Large fairy sprite (96x96) & glowing collectibles
*     - 3 non-duplicating power-ups per level (Double Jump, Super Jump, Glide)
*     - Open-Meteo Weather integration + [L] key toggle
*
********************************************************************************************/

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
    #include <emscripten/fetch.h>
#endif

// =========================================================================================
// SECTION 1: ENUMS AND DATA STRUCTURES
// =========================================================================================

typedef enum {
    STATE_FETCHING,
    STATE_START,
    STATE_PLAY,
    STATE_GAME_OVER
} GameState;

typedef enum {
    WEATHER_CLEAR,
    WEATHER_CLOUDY,
    WEATHER_RAINY
} WeatherType;

typedef enum {
    POWERUP_DOUBLE_JUMP,
    POWERUP_SUPER_JUMP,
    POWERUP_GLIDE
} PowerupType;

typedef struct {
    Rectangle rect;
    float speedX;
    float minX;
    float maxX;
} Platform;

typedef struct {
    Vector2 pos;
    int boundPlatformIdx;
    float offsetX;
    bool collected;
} SparkleOrb;

typedef struct {
    Vector2 pos;
    PowerupType type;
    bool active;
    float timer;
} PowerupItem;

typedef struct {
    Vector2 pos;
    float speed;
} Raindrop;

// =========================================================================================
// SECTION 2: CONSTANTS & GLOBAL GAME STATE
// =========================================================================================

#define MAX_PLATFORMS 8
#define MAX_ORBS 6
#define MAX_POWERUPS 3
#define MAX_RAINDROPS 140

static const int screenWidth = 1920;
static const int screenHeight = 1080;

static GameState currentState = STATE_FETCHING;
static WeatherType currentWeather = WEATHER_CLEAR;
static char weatherName[128] = "Checking Skies...";
static int currentLevel = 1;
static int score = 0;

static int activePlatformCount = 5;
static int activeOrbCount = 3;

// Fairy Variables
static Texture2D fairyTexture;
static Vector2 fairyPos = { 960.0f, 850.0f };
static Vector2 fairyVel = { 0.0f, 0.0f };
static const float fairyRadius = 26.0f;
static float fairyGravity = 860.0f;
static float jumpStrength = -720.0f;

// 3 Distinct Power-ups (Reset each level)
static bool hasDoubleJumpUnlocked = false;
static bool hasUsedDoubleJump = false;
static bool hasSuperJumpUnlocked = false;
static bool hasGlideUnlocked = false;
static bool isGliding = false;

// Entities
static Platform platforms[MAX_PLATFORMS];
static SparkleOrb orbs[MAX_ORBS];
static PowerupItem powerups[MAX_POWERUPS];
static Raindrop raindrops[MAX_RAINDROPS];
static float powerupSpawnTimer = 0.0f;

// Sky Palette
static Color skyTopColor = { 135, 206, 250, 255 };
static Color skyBottomColor = { 255, 230, 240, 255 };

// =========================================================================================
// SECTION 3: WEATHER & WEB REQUEST LOGIC
// =========================================================================================

void ApplyWeather(WeatherType type) {
    currentWeather = type;
    if (type == WEATHER_CLEAR) {
        strcpy(weatherName, "Sunny Grove (Light & Floaty)");
        skyTopColor = (Color){ 120, 200, 255, 255 };
        skyBottomColor = (Color){ 255, 235, 200, 255 };
        fairyGravity = 800.0f;
    } else if (type == WEATHER_CLOUDY) {
        strcpy(weatherName, "Overcast Twilight (Balanced Drift)");
        skyTopColor = (Color){ 105, 115, 135, 255 };
        skyBottomColor = (Color){ 180, 175, 195, 255 };
        fairyGravity = 860.0f;
    } else if (type == WEATHER_RAINY) {
        strcpy(weatherName, "Rainy Glade (Heavy Air Drag)");
        skyTopColor = (Color){ 35, 45, 65, 255 };
        skyBottomColor = (Color){ 75, 85, 105, 255 };
        fairyGravity = 960.0f;
    }
}

#if defined(PLATFORM_WEB)
void OnFetchSuccess(emscripten_fetch_t *fetch) {
    char *data = (char *)malloc(fetch->numBytes + 1);
    memcpy(data, fetch->data, fetch->numBytes);
    data[fetch->numBytes] = '\0';

    int code = 0;
    char *codePtr = strstr(data, "\"weather_code\":");
    if (codePtr != NULL) {
        code = atoi(codePtr + 15);
    }
    free(data);

    if (code >= 51) ApplyWeather(WEATHER_RAINY);
    else if (code >= 2) ApplyWeather(WEATHER_CLOUDY);
    else ApplyWeather(WEATHER_CLEAR);

    currentState = STATE_START;
    emscripten_fetch_close(fetch);
}

void OnFetchFail(emscripten_fetch_t *fetch) {
    ApplyWeather(WEATHER_CLEAR);
    currentState = STATE_START;
    emscripten_fetch_close(fetch);
}

void FetchWeatherData(void) {
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = OnFetchSuccess;
    attr.onerror = OnFetchFail;

    emscripten_fetch(&attr, "https://api.open-meteo.com/v1/forecast?latitude=51.5074&longitude=-0.1278&current=weather_code");
}
#endif

// =========================================================================================
// SECTION 4: SCALED DIFFICULTY PROCEDURAL GENERATOR
// =========================================================================================

void GenerateLevel(void) {
    hasDoubleJumpUnlocked = false;
    hasUsedDoubleJump = false;
    hasSuperJumpUnlocked = false;
    hasGlideUnlocked = false;
    isGliding = false;
    powerupSpawnTimer = 0.0f;

    for (int i = 0; i < MAX_POWERUPS; i++) {
        powerups[i].active = false;
        powerups[i].timer = 0.0f;
    }

    activePlatformCount = 4 + currentLevel;
    if (activePlatformCount > MAX_PLATFORMS) activePlatformCount = MAX_PLATFORMS;

    activeOrbCount = 2 + currentLevel;
    if (activeOrbCount > MAX_ORBS) activeOrbCount = MAX_ORBS;

    // Platform 0: Ground Base (Shrinks slightly on later levels, but stays generous)
    float baseWidth = 520.0f - (float)(currentLevel * 15);
    if (baseWidth < 360.0f) baseWidth = 360.0f;
    platforms[0].rect = (Rectangle){ (screenWidth - baseWidth) * 0.5f, 950.0f, baseWidth, 24.0f };
    platforms[0].speedX = 0.0f;
    platforms[0].minX = 0.0f;
    platforms[0].maxX = (float)screenWidth;

    // Progression parameters:
    // Vertical rise starts at 130px on Level 1, growing by 8px per level up to 175px
    float verticalStep = 130.0f + (float)((currentLevel - 1) * 8);
    if (verticalStep > 175.0f) verticalStep = 175.0f;

    // Horizontal stride: starts at ~290px-370px on Lvl 1, climbing to ~380px-500px on later levels
    float minStride = 290.0f + (float)((currentLevel - 1) * 20);
    float maxStride = 370.0f + (float)((currentLevel - 1) * 26);
    if (minStride > 390.0f) minStride = 390.0f;
    if (maxStride > 510.0f) maxStride = 510.0f;

    float currentY = 950.0f - verticalStep;
    float currentCenterX = 960.0f;
    int direction = (rand() % 2 == 0) ? 1 : -1;

    for (int i = 1; i < activePlatformCount; i++) {
        // Platform width shrinks with level progression
        float width = (float)(320 - (currentLevel * 16) + (rand() % 30));
        if (width < 190.0f) width = 190.0f;

        // Spread platforms out further based on the level's stride range
        float stepDist = minStride + (float)(rand() % (int)(maxStride - minStride + 1.0f));
        float nextCenterX = currentCenterX + (direction * stepDist);

        // Border bounce logic
        if (nextCenterX > (float)screenWidth - 300.0f) {
            nextCenterX = currentCenterX - stepDist;
            direction = -1;
        } else if (nextCenterX < 300.0f) {
            nextCenterX = currentCenterX + stepDist;
            direction = 1;
        } else {
            if (rand() % 10 < 8) direction = -direction;
        }

        float platX = nextCenterX - (width * 0.5f);
        platforms[i].rect = (Rectangle){ platX, currentY, width, 22.0f };

        // Speed increases with level
        float speed = (float)(55 + (rand() % 25) + (currentLevel * 10));
        platforms[i].speedX = (rand() % 2 == 0) ? speed : -speed;

        float sweep = (float)(60 + (rand() % 30) + (currentLevel * 6));
        platforms[i].minX = (platX - sweep < 40.0f) ? 40.0f : platX - sweep;
        platforms[i].maxX = (platX + width + sweep > (float)screenWidth - 40.0f) 
                            ? (float)screenWidth - 40.0f : platX + width + sweep;

        currentCenterX = nextCenterX;
        currentY -= verticalStep;
    }

    // Place Orbs above platforms
    for (int i = 0; i < activeOrbCount; i++) {
        int platIdx = 1 + (i % (activePlatformCount - 1));
        orbs[i].boundPlatformIdx = platIdx;
        orbs[i].offsetX = platforms[platIdx].rect.width * 0.5f;
        orbs[i].pos = (Vector2){
            platforms[platIdx].rect.x + orbs[i].offsetX,
            platforms[platIdx].rect.y - 44.0f
        };
        orbs[i].collected = false;
    }
}

void InitOrResetGame(void) {
    fairyPos = (Vector2){ 960.0f, 890.0f };
    fairyVel = (Vector2){ 0.0f, 0.0f };
    score = 0;
    currentLevel = 1;

    for (int i = 0; i < MAX_RAINDROPS; i++) {
        raindrops[i].pos = (Vector2){ (float)(rand() % screenWidth), (float)(rand() % screenHeight) };
        raindrops[i].speed = (float)(400 + rand() % 300);
    }

    GenerateLevel();
    currentState = STATE_PLAY;
}

// =========================================================================================
// SECTION 5: FRAME UPDATE (PHYSICS, COLLISIONS, LOGIC)
// =========================================================================================

void UpdateDrawFrame(void) {
    float dt = GetFrameTime();

    if (IsKeyPressed(KEY_L)) {
        if (currentWeather == WEATHER_CLEAR) ApplyWeather(WEATHER_CLOUDY);
        else if (currentWeather == WEATHER_CLOUDY) ApplyWeather(WEATHER_RAINY);
        else ApplyWeather(WEATHER_CLEAR);
    }

    // -------------------------------------------------------------
    // LOGIC & INPUT
    // -------------------------------------------------------------
    if (currentState == STATE_START) {
        if (IsKeyPressed(KEY_SPACE)) InitOrResetGame();
    } 
    else if (currentState == STATE_PLAY) {
        // --- 1. Move Platforms ---
        for (int i = 0; i < activePlatformCount; i++) {
            if (platforms[i].speedX == 0.0f) continue;

            platforms[i].rect.x += platforms[i].speedX * dt;

            if (platforms[i].rect.x < platforms[i].minX) {
                platforms[i].rect.x = platforms[i].minX;
                platforms[i].speedX = (float)fabs(platforms[i].speedX);
            } 
            else if (platforms[i].rect.x + platforms[i].rect.width > platforms[i].maxX) {
                platforms[i].rect.x = platforms[i].maxX - platforms[i].rect.width;
                platforms[i].speedX = -(float)fabs(platforms[i].speedX);
            }
        }

        // --- 2. Anchor Orbs to Moving Platforms ---
        for (int i = 0; i < activeOrbCount; i++) {
            if (!orbs[i].collected) {
                int pIdx = orbs[i].boundPlatformIdx;
                orbs[i].pos.x = platforms[pIdx].rect.x + orbs[i].offsetX;
            }
        }

        // --- 3. Super Jump Value ---
        jumpStrength = hasSuperJumpUnlocked ? -880.0f : -720.0f;

        // --- 4. Horizontal Input & Lateral Collisions ---
        float moveX = 0.0f;
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) moveX -= 1.0f;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) moveX += 1.0f;

        float nextPosX = fairyPos.x + (moveX * 400.0f * dt);

        if (nextPosX < 0) nextPosX = (float)screenWidth;
        if (nextPosX > (float)screenWidth) nextPosX = 0;

        for (int i = 0; i < activePlatformCount; i++) {
            if (fairyPos.y + fairyRadius > platforms[i].rect.y + 6.0f &&
                fairyPos.y - fairyRadius < platforms[i].rect.y + platforms[i].rect.height - 6.0f) {
                
                if (moveX > 0 && fairyPos.x <= platforms[i].rect.x && nextPosX + fairyRadius >= platforms[i].rect.x) {
                    nextPosX = platforms[i].rect.x - fairyRadius;
                }
                else if (moveX < 0 && fairyPos.x >= platforms[i].rect.x + platforms[i].rect.width &&
                         nextPosX - fairyRadius <= platforms[i].rect.x + platforms[i].rect.width) {
                    nextPosX = platforms[i].rect.x + platforms[i].rect.width + fairyRadius;
                }
            }
        }
        fairyPos.x = nextPosX;

        // --- 5. Jump Inputs ---
        bool jumpPressed = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W);
        bool jumpHeld    = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_UP) || IsKeyDown(KEY_W);

        if (jumpPressed) {
            if (fabs(fairyVel.y) < 30.0f) { // Grounded jump
                fairyVel.y = jumpStrength;
                hasUsedDoubleJump = false;
            } 
            else if (hasDoubleJumpUnlocked && !hasUsedDoubleJump) { // Air jump
                fairyVel.y = jumpStrength * 0.95f;
                hasUsedDoubleJump = true;
            }
        }

        // --- 6. Glide Feature ---
        isGliding = false;
        if (hasGlideUnlocked && jumpHeld && fairyVel.y > 80.0f) {
            isGliding = true;
            fairyVel.y = 120.0f;
        } else {
            fairyVel.y += fairyGravity * dt;
        }

        float prevPosY = fairyPos.y;
        fairyPos.y += fairyVel.y * dt;

        // --- 7. Platform Top Landings & Ceiling Head Bonks ---
        for (int i = 0; i < activePlatformCount; i++) {
            bool horizontallyAligned = (fairyPos.x + fairyRadius - 8.0f >= platforms[i].rect.x) && 
                                       (fairyPos.x - fairyRadius + 8.0f <= platforms[i].rect.x + platforms[i].rect.width);

            if (horizontallyAligned) {
                // Landing on Top
                if (fairyVel.y >= 0 && prevPosY + fairyRadius <= platforms[i].rect.y + 18.0f &&
                    fairyPos.y + fairyRadius >= platforms[i].rect.y) {
                    
                    fairyPos.y = platforms[i].rect.y - fairyRadius;
                    fairyVel.y = 0;
                    hasUsedDoubleJump = false;
                    fairyPos.x += platforms[i].speedX * dt;
                }
                // Underside Bonk
                else if (fairyVel.y < 0 && prevPosY - fairyRadius >= platforms[i].rect.y + platforms[i].rect.height - 18.0f &&
                         fairyPos.y - fairyRadius <= platforms[i].rect.y + platforms[i].rect.height) {
                    
                    fairyPos.y = platforms[i].rect.y + platforms[i].rect.height + fairyRadius;
                    fairyVel.y = 80.0f;
                }
            }
        }

        // --- 8. Orb Collection & Next Level Trigger ---
        bool allCollected = true;
        for (int i = 0; i < activeOrbCount; i++) {
            if (!orbs[i].collected) {
                if (CheckCollisionCircles(fairyPos, fairyRadius, orbs[i].pos, 24.0f)) {
                    orbs[i].collected = true;
                    score += 100;
                } else {
                    allCollected = false;
                }
            }
        }

        if (allCollected) {
            currentLevel++;
            score += 300;
            fairyPos = (Vector2){ 960.0f, 890.0f };
            fairyVel = (Vector2){ 0.0f, 0.0f };
            GenerateLevel(); // Re-generates with wider spacing and narrower ledges
        }

        // --- 9. Power-Up Spawner (Only Uncollected Power-Ups) ---
        powerupSpawnTimer += dt;
        if (powerupSpawnTimer > 7.0f) {
            powerupSpawnTimer = 0.0f;

            PowerupType availableTypes[3];
            int availableCount = 0;

            if (!hasDoubleJumpUnlocked) availableTypes[availableCount++] = POWERUP_DOUBLE_JUMP;
            if (!hasSuperJumpUnlocked)  availableTypes[availableCount++] = POWERUP_SUPER_JUMP;
            if (!hasGlideUnlocked)      availableTypes[availableCount++] = POWERUP_GLIDE;

            if (availableCount > 0) {
                for (int i = 0; i < MAX_POWERUPS; i++) {
                    if (!powerups[i].active) {
                        powerups[i].active = true;
                        powerups[i].timer = 12.0f;
                        powerups[i].type = availableTypes[rand() % availableCount];

                        int platIdx = 1 + (rand() % (activePlatformCount - 1));
                        powerups[i].pos = (Vector2){ platforms[platIdx].rect.x + 45.0f, platforms[platIdx].rect.y - 36.0f };
                        break;
                    }
                }
            }
        }

        for (int i = 0; i < MAX_POWERUPS; i++) {
            if (powerups[i].active) {
                powerups[i].timer -= dt;
                if (powerups[i].timer <= 0.0f) powerups[i].active = false;

                if (CheckCollisionCircles(fairyPos, fairyRadius, powerups[i].pos, 26.0f)) {
                    if (powerups[i].type == POWERUP_DOUBLE_JUMP) {
                        hasDoubleJumpUnlocked = true;
                        hasUsedDoubleJump = false;
                    } else if (powerups[i].type == POWERUP_SUPER_JUMP) {
                        hasSuperJumpUnlocked = true;
                    } else if (powerups[i].type == POWERUP_GLIDE) {
                        hasGlideUnlocked = true;
                    }
                    score += 50;
                    powerups[i].active = false;
                }
            }
        }

        // Bottom death pit
        if (fairyPos.y > (float)screenHeight + 80) {
            currentState = STATE_GAME_OVER;
        }

        // Raindrop simulation
        if (currentWeather == WEATHER_RAINY) {
            for (int i = 0; i < MAX_RAINDROPS; i++) {
                raindrops[i].pos.y += raindrops[i].speed * dt;
                if (raindrops[i].pos.y > (float)screenHeight) {
                    raindrops[i].pos.y = -15.0f;
                    raindrops[i].pos.x = (float)(rand() % screenWidth);
                }
            }
        }
    } 
    else if (currentState == STATE_GAME_OVER) {
        if (IsKeyPressed(KEY_R)) InitOrResetGame();
    }

    // =============================================================
    // SECTION 6: RENDERING & VISUAL FEEDBACK
    // =============================================================
    BeginDrawing();
    DrawRectangleGradientV(0, 0, screenWidth, screenHeight, skyTopColor, skyBottomColor);

    if (currentState == STATE_FETCHING) {
        DrawText("Tuning into real-world skies...", screenWidth / 2 - 280, screenHeight / 2 - 20, 36, RAYWHITE);
    } 
    else if (currentState == STATE_START) {
        DrawText("FAIRY SKY GLADE", screenWidth / 2 - 320, 260, 68, RAYWHITE);
        DrawText(TextFormat("Atmosphere: %s", weatherName), 
                 screenWidth / 2 - MeasureText(TextFormat("Atmosphere: %s", weatherName), 28) / 2, 370, 28, YELLOW);
        DrawText("Controls: [A][D] Move | [SPACE/W] Jump | [L] Toggle Weather", screenWidth / 2 - 420, 450, 28, RAYWHITE);
        DrawText("Press [SPACE] to Begin Adventure", screenWidth / 2 - 270, 580, 32, GREEN);
    } 
    else if (currentState == STATE_PLAY) {
        // Draw Raindrops
        if (currentWeather == WEATHER_RAINY) {
            for (int i = 0; i < MAX_RAINDROPS; i++) {
                DrawLine((int)raindrops[i].pos.x, (int)raindrops[i].pos.y, 
                         (int)raindrops[i].pos.x, (int)raindrops[i].pos.y + 14, (Color){ 200, 220, 255, 180 });
            }
        }

        // Draw Platforms
        for (int i = 0; i < activePlatformCount; i++) {
            DrawRectangleRec(platforms[i].rect, (Color){ 240, 130, 190, 255 });
            DrawRectangleLinesEx(platforms[i].rect, 3, (Color){ 120, 45, 150, 255 });
        }

        // Draw Orbs
        for (int i = 0; i < activeOrbCount; i++) {
            if (!orbs[i].collected) {
                DrawCircleV(orbs[i].pos, 22.0f, (Color){ 255, 215, 0, 180 });
                DrawCircleV(orbs[i].pos, 15.0f, GOLD);
                DrawCircleLines((int)orbs[i].pos.x, (int)orbs[i].pos.y, 24.0f, YELLOW);
            }
        }

        // Draw Power-ups
        for (int i = 0; i < MAX_POWERUPS; i++) {
            if (powerups[i].active) {
                if (powerups[i].type == POWERUP_DOUBLE_JUMP) {
                    DrawPoly(powerups[i].pos, 4, 24.0f, 45.0f, SKYBLUE);
                    DrawPolyLines(powerups[i].pos, 4, 25.0f, 45.0f, WHITE);
                    DrawText("2x", (int)powerups[i].pos.x - 11, (int)powerups[i].pos.y - 10, 22, DARKBLUE);
                } 
                else if (powerups[i].type == POWERUP_SUPER_JUMP) {
                    DrawPoly(powerups[i].pos, 5, 24.0f, 0.0f, GOLD);
                    DrawPolyLines(powerups[i].pos, 5, 26.0f, 0.0f, YELLOW);
                    DrawText("^", (int)powerups[i].pos.x - 7, (int)powerups[i].pos.y - 17, 30, RED);
                } 
                else if (powerups[i].type == POWERUP_GLIDE) {
                    DrawCircleV(powerups[i].pos, 22.0f, MAGENTA);
                    DrawCircleLines((int)powerups[i].pos.x, (int)powerups[i].pos.y, 24.0f, PINK);
                    DrawText("~", (int)powerups[i].pos.x - 7, (int)powerups[i].pos.y - 20, 32, WHITE);
                }
            }
        }

        // Visual Auras
        if (hasSuperJumpUnlocked) {
            DrawCircleLines((int)fairyPos.x, (int)fairyPos.y, fairyRadius + 12.0f, GOLD);
        }
        if (hasDoubleJumpUnlocked && !hasUsedDoubleJump) {
            DrawCircleLines((int)fairyPos.x, (int)fairyPos.y, fairyRadius + 8.0f, SKYBLUE);
        }
        if (isGliding) {
            DrawLine((int)fairyPos.x - 30, (int)fairyPos.y + 22, (int)fairyPos.x + 30, (int)fairyPos.y + 22, PINK);
            DrawText("Gliding...", (int)fairyPos.x - 38, (int)fairyPos.y - 50, 20, PINK);
        }

        // Render Fairy (96x96 pixels)
        if (fairyTexture.id > 0) {
            Rectangle sourceRec = { 0.0f, 0.0f, (float)fairyTexture.width, (float)fairyTexture.height };
            Rectangle destRec = { fairyPos.x, fairyPos.y, 96.0f, 96.0f };
            Vector2 origin = { 48.0f, 48.0f };
            DrawTexturePro(fairyTexture, sourceRec, destRec, origin, 0.0f, WHITE);
        } else {
            DrawCircleV((Vector2){ fairyPos.x - 20.0f, fairyPos.y - 10.0f }, 18.0f, (Color){ 240, 240, 255, 180 });
            DrawCircleV((Vector2){ fairyPos.x + 20.0f, fairyPos.y - 10.0f }, 18.0f, (Color){ 240, 240, 255, 180 });
            DrawCircleV(fairyPos, fairyRadius, (Color){ 255, 182, 193, 255 });
            DrawCircleLines((int)fairyPos.x, (int)fairyPos.y, fairyRadius, WHITE);
        }

        // HUD Text
        DrawText(TextFormat("Level: %d  |  Score: %d", currentLevel, score), 36, 28, 32, WHITE);
        DrawText(TextFormat("Atmosphere: %s [L to switch]", weatherName), 36, 68, 22, RAYWHITE);

        if (hasDoubleJumpUnlocked) {
            DrawText(hasUsedDoubleJump ? "[DOUBLE JUMP: EXHAUSTED]" : "[DOUBLE JUMP: READY]", 36, 102, 20, SKYBLUE);
        }
        if (hasSuperJumpUnlocked) {
            DrawText("[SUPER JUMP: ACTIVE]", 36, 130, 20, GOLD);
        }
        if (hasGlideUnlocked) {
            DrawText("[FEATHER GLIDE: ACTIVE]", 36, 158, 20, PINK);
        }
    } 
    else if (currentState == STATE_GAME_OVER) {
        DrawText("THE FAIRY FELL!", screenWidth / 2 - 260, 360, 64, RED);
        DrawText(TextFormat("Reached Level %d with %d Points", currentLevel, score), screenWidth / 2 - 230, 460, 30, RAYWHITE);
        DrawText("Press [R] to Try Again", screenWidth / 2 - 190, 540, 32, YELLOW);
    }

    EndDrawing();
}

// =========================================================================================
// SECTION 7: PROGRAM ENTRY POINT
// =========================================================================================

int main(void) {
    srand((unsigned int)time(NULL));

    InitWindow(screenWidth, screenHeight, "Fairy Sky Glade");
    SetTargetFPS(60);

    fairyTexture = LoadTexture("fairy.png");
    if (fairyTexture.id == 0) {
        fairyTexture = LoadTexture("C:/Users/2401317/Documents/GitHub/RaylibFairyGame/Game/fairy.png");
    }

#if defined(PLATFORM_WEB)
    FetchWeatherData();
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    ApplyWeather(WEATHER_CLEAR);
    currentState = STATE_START;

    while (!WindowShouldClose()) {
        UpdateDrawFrame();
    }

    UnloadTexture(fairyTexture);
    CloseWindow();
#endif

    return 0;
}