/*******************************************************************************************
*
*   FAIRY SKY GLADE - Micro-Game in C (Raylib + WebAssembly)
*
*   Features:
*     - Auto-scaling window (compatible with 1080p, 720p, and high-DPI laptop displays)
*     - Background Music Streaming (BackgroundMusic file integration with auto-loop)
*     - Strict ceiling clamping: platforms NEVER spawn above top of screen
*     - Non-overlapping platform layout validation with progressive difficulty
*     - Custom textured assets (Fairy, Platform, Diamond, Star, Feather)
*     - Non-duplicating active power-ups (Double Jump, Super Jump, Feather Soft-Landing)
*     - Open-Meteo Weather integration + [L] key toggle
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
    POWERUP_FEATHER_FALL
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

#if defined(PLATFORM_WEB)
static GameState currentState = STATE_FETCHING;
#else
static GameState currentState = STATE_START;
#endif

static WeatherType currentWeather = WEATHER_CLEAR;
static char weatherName[128] = "Checking Skies...";
static int currentLevel = 1;
static int score = 0;

static int activePlatformCount = 5;
static int activeOrbCount = 3;

// Asset Textures
static Texture2D fairyTexture;
static Texture2D platformTexture;
static Texture2D texDoubleJump;
static Texture2D texSuperJump;
static Texture2D texFeather;

// Background Music
static Music bgMusic;
static bool musicReady = false;

// Fairy Variables
static Vector2 fairyPos = { 960.0f, 850.0f };
static Vector2 fairyVel = { 0.0f, 0.0f };
static const float fairyRadius = 26.0f;
static float fairyGravity = 1100.0f;
static float jumpStrength = -640.0f;

// Power-ups
static bool hasDoubleJumpUnlocked = false;
static bool hasUsedDoubleJump = false;
static bool hasSuperJumpUnlocked = false;
static bool hasFeatherFallUnlocked = false;

// Entities
static Platform platforms[MAX_PLATFORMS];
static SparkleOrb orbs[MAX_ORBS];
static PowerupItem powerups[MAX_POWERUPS];
static Raindrop raindrops[MAX_RAINDROPS];
static float powerupSpawnTimer = 0.0f;

// Sky Palette
static Color skyTopColor = { 135, 206, 250, 255 };
static Color skyBottomColor = { 255, 230, 240, 255 };

// Scaling Target
static RenderTexture2D target;

// =========================================================================================
// SECTION 3: WEATHER & WEB REQUEST LOGIC
// =========================================================================================

void ApplyWeather(WeatherType type) {
    currentWeather = type;
    if (type == WEATHER_CLEAR) {
        strcpy(weatherName, "Sunny Grove (Crisp Jumps)");
        skyTopColor = (Color){ 120, 200, 255, 255 };
        skyBottomColor = (Color){ 255, 235, 200, 255 };
        fairyGravity = 1080.0f;
    } else if (type == WEATHER_CLOUDY) {
        strcpy(weatherName, "Overcast Twilight (Moderate Drift)");
        skyTopColor = (Color){ 105, 115, 135, 255 };
        skyBottomColor = (Color){ 180, 175, 195, 255 };
        fairyGravity = 1140.0f;
    } else if (type == WEATHER_RAINY) {
        strcpy(weatherName, "Rainy Glade (Heavy Air Downforce)");
        skyTopColor = (Color){ 35, 45, 65, 255 };
        skyBottomColor = (Color){ 75, 85, 105, 255 };
        fairyGravity = 1220.0f;
    }
}

#if defined(PLATFORM_WEB)
void OnFetchSuccess(emscripten_fetch_t *fetch) {
    char *data = (char *)malloc(fetch->numBytes + 1);
    if (data) {
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
    }

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
// SECTION 4: PROCEDURAL GENERATOR (STRICT TOP CEILING BOUNDS)
// =========================================================================================

void TrySpawnPowerup(void) {
    PowerupType availableTypes[3];
    int availableCount = 0;

    if (!hasDoubleJumpUnlocked)  availableTypes[availableCount++] = POWERUP_DOUBLE_JUMP;
    if (!hasSuperJumpUnlocked)   availableTypes[availableCount++] = POWERUP_SUPER_JUMP;
    if (!hasFeatherFallUnlocked) availableTypes[availableCount++] = POWERUP_FEATHER_FALL;

    if (availableCount > 0) {
        for (int i = 0; i < MAX_POWERUPS; i++) {
            if (!powerups[i].active) {
                powerups[i].active = true;
                powerups[i].timer = 16.0f;
                powerups[i].type = availableTypes[rand() % availableCount];

                int platIdx = 1 + (rand() % (activePlatformCount - 1));
                powerups[i].pos = (Vector2){ 
                    platforms[platIdx].rect.x + (platforms[platIdx].rect.width * 0.5f), 
                    platforms[platIdx].rect.y - 38.0f 
                };
                break;
            }
        }
    }
}

void GenerateLevel(void) {
    hasDoubleJumpUnlocked = false;
    hasUsedDoubleJump = false;
    hasSuperJumpUnlocked = false;
    hasFeatherFallUnlocked = false;
    powerupSpawnTimer = 0.0f;

    for (int i = 0; i < MAX_POWERUPS; i++) {
        powerups[i].active = false;
        powerups[i].timer = 0.0f;
    }

    activePlatformCount = 4 + currentLevel;
    if (activePlatformCount > MAX_PLATFORMS) activePlatformCount = MAX_PLATFORMS;

    activeOrbCount = 2 + currentLevel;
    if (activeOrbCount > MAX_ORBS) activeOrbCount = MAX_ORBS;

    // Platform 0: Ground Base
    float baseWidth = 480.0f - (float)(currentLevel * 20);
    if (baseWidth < 280.0f) baseWidth = 280.0f;
    platforms[0].rect = (Rectangle){ (screenWidth - baseWidth) * 0.5f, 960.0f, baseWidth, 26.0f };
    platforms[0].speedX = 0.0f;
    platforms[0].minX = 0.0f;
    platforms[0].maxX = (float)screenWidth;

    float lowestY = 960.0f;
    float highestSafeY = 180.0f;
    float totalVerticalSpan = lowestY - highestSafeY;
    float verticalStep = totalVerticalSpan / (float)(activePlatformCount);

    if (verticalStep > 138.0f) verticalStep = 138.0f;
    if (verticalStep < 110.0f) verticalStep = 110.0f;

    float currentY = lowestY - verticalStep;
    int direction = (rand() % 2 == 0) ? 1 : -1;

    for (int i = 1; i < activePlatformCount; i++) {
        float width = (float)(300 - (currentLevel * 22) + (rand() % 20));
        if (width < 140.0f) width = 140.0f;

        float platX = 0.0f;
        bool validPosition = false;
        int attempts = 0;

        while (!validPosition && attempts < 100) {
            attempts++;

            float stepDist = 240.0f + (float)(rand() % 160);
            float prevCenterX = platforms[i - 1].rect.x + (platforms[i - 1].rect.width * 0.5f);
            float nextCenterX = prevCenterX + (direction * stepDist);

            if (nextCenterX > (float)screenWidth - 240.0f) {
                direction = -1;
                nextCenterX = prevCenterX + (direction * stepDist);
            } else if (nextCenterX < 240.0f) {
                direction = 1;
                nextCenterX = prevCenterX + (direction * stepDist);
            }

            platX = nextCenterX - (width * 0.5f);
            Rectangle candidateRec = { platX, currentY, width, 24.0f };

            bool overlaps = false;
            for (int j = 0; j < i; j++) {
                Rectangle expandedCheck = {
                    candidateRec.x - 60.0f,
                    candidateRec.y - 80.0f,
                    candidateRec.width + 120.0f,
                    candidateRec.height + 160.0f
                };

                if (CheckCollisionRecs(expandedCheck, platforms[j].rect)) {
                    overlaps = true;
                    break;
                }
            }

            if (!overlaps) {
                validPosition = true;
            } else {
                direction = -direction;
            }
        }

        if (!validPosition) {
            platX = (direction == 1) ? (float)(screenWidth - 350) : 200.0f;
        }

        platforms[i].rect = (Rectangle){ platX, currentY, width, 24.0f };

        float speed = (float)(50 + (rand() % 25) + (currentLevel * 14));
        platforms[i].speedX = (rand() % 2 == 0) ? speed : -speed;

        float sweep = (float)(50 + (rand() % 30) + (currentLevel * 5));
        platforms[i].minX = (platX - sweep < 40.0f) ? 40.0f : platX - sweep;
        platforms[i].maxX = (platX + width + sweep > (float)screenWidth - 40.0f) 
                            ? (float)screenWidth - 40.0f : platX + width + sweep;

        direction = -direction;
        currentY -= verticalStep;
    }

    for (int i = 0; i < activeOrbCount; i++) {
        int platIdx = 1 + (i % (activePlatformCount - 1));
        orbs[i].boundPlatformIdx = platIdx;
        orbs[i].offsetX = platforms[platIdx].rect.width * 0.5f;
        orbs[i].pos = (Vector2){
            platforms[platIdx].rect.x + orbs[i].offsetX,
            platforms[platIdx].rect.y - 46.0f
        };
        orbs[i].collected = false;
    }

    TrySpawnPowerup();
}

void InitOrResetGame(void) {
    fairyPos = (Vector2){ 960.0f, 900.0f };
    fairyVel = (Vector2){ 0.0f, 0.0f };
    score = 0;
    currentLevel = 1;

    for (int i = 0; i < MAX_RAINDROPS; i++) {
        raindrops[i].pos = (Vector2){ (float)(rand() % screenWidth), (float)(rand() % screenHeight) };
        raindrops[i].speed = (float)(400 + rand() % 300);
    }

    if (musicReady) {
        SeekMusicStream(bgMusic, 0.0f);
        if (!IsMusicStreamPlaying(bgMusic)) PlayMusicStream(bgMusic);
    }

    GenerateLevel();
    currentState = STATE_PLAY;
}

// =========================================================================================
// SECTION 5: FRAME UPDATE (PHYSICS, COLLISIONS, LOGIC)
// =========================================================================================

void UpdateDrawFrame(void) {
    float dt = GetFrameTime();
    if (dt > 0.05f) dt = 0.05f;

    // Stream background music buffer continuously
    if (musicReady) {
        UpdateMusicStream(bgMusic);
    }

    if (IsKeyPressed(KEY_L)) {
        if (currentWeather == WEATHER_CLEAR) ApplyWeather(WEATHER_CLOUDY);
        else if (currentWeather == WEATHER_CLOUDY) ApplyWeather(WEATHER_RAINY);
        else ApplyWeather(WEATHER_CLEAR);
    }

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

        // --- 2. Anchor Orbs to Platforms ---
        for (int i = 0; i < activeOrbCount; i++) {
            if (!orbs[i].collected) {
                int pIdx = orbs[i].boundPlatformIdx;
                orbs[i].pos.x = platforms[pIdx].rect.x + orbs[i].offsetX;
            }
        }

        // --- 3. Jump Strength ---
        jumpStrength = hasSuperJumpUnlocked ? -800.0f : -640.0f;

        // --- 4. Horizontal Input & Screen Wrapping ---
        float moveX = 0.0f;
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) moveX -= 1.0f;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) moveX += 1.0f;

        float nextPosX = fairyPos.x + (moveX * 430.0f * dt);

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

        if (jumpPressed) {
            if (fabs(fairyVel.y) < 40.0f) {
                fairyVel.y = jumpStrength;
                hasUsedDoubleJump = false;
            } 
            else if (hasDoubleJumpUnlocked && !hasUsedDoubleJump) {
                fairyVel.y = jumpStrength * 0.95f;
                hasUsedDoubleJump = true;
            }
        }

        fairyVel.y += fairyGravity * dt;

        if (hasFeatherFallUnlocked && fairyVel.y > 380.0f) {
            fairyVel.y = 380.0f;
        }

        float prevPosY = fairyPos.y;
        fairyPos.y += fairyVel.y * dt;

        // --- 6. Platform Landings ---
        for (int i = 0; i < activePlatformCount; i++) {
            bool horizontallyAligned = (fairyPos.x + fairyRadius - 8.0f >= platforms[i].rect.x) && 
                                       (fairyPos.x - fairyRadius + 8.0f <= platforms[i].rect.x + platforms[i].rect.width);

            if (horizontallyAligned) {
                if (fairyVel.y >= 0 && prevPosY + fairyRadius <= platforms[i].rect.y + 22.0f &&
                    fairyPos.y + fairyRadius >= platforms[i].rect.y) {
                    
                    fairyPos.y = platforms[i].rect.y - fairyRadius;
                    fairyVel.y = 0;
                    hasUsedDoubleJump = false;
                    fairyPos.x += platforms[i].speedX * dt;
                }
                else if (fairyVel.y < 0 && prevPosY - fairyRadius >= platforms[i].rect.y + platforms[i].rect.height - 18.0f &&
                         fairyPos.y - fairyRadius <= platforms[i].rect.y + platforms[i].rect.height) {
                    
                    fairyPos.y = platforms[i].rect.y + platforms[i].rect.height + fairyRadius;
                    fairyVel.y = 80.0f;
                }
            }
        }

        // --- 7. Collect Orbs ---
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
            fairyPos = (Vector2){ 960.0f, 900.0f };
            fairyVel = (Vector2){ 0.0f, 0.0f };
            GenerateLevel();
        }

        // --- 8. Power-up Spawn Timer ---
        powerupSpawnTimer += dt;
        if (powerupSpawnTimer > 5.0f) {
            powerupSpawnTimer = 0.0f;
            TrySpawnPowerup();
        }

        // --- 9. Collect Power-ups ---
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
                    } else if (powerups[i].type == POWERUP_FEATHER_FALL) {
                        hasFeatherFallUnlocked = true;
                    }
                    score += 50;
                    powerups[i].active = false;
                }
            }
        }

        if (fairyPos.y > (float)screenHeight + 80) {
            currentState = STATE_GAME_OVER;
        }

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
    // SECTION 6: RENDERING VIA SCALED TARGET
    // =============================================================
    BeginTextureMode(target);
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
        if (currentWeather == WEATHER_RAINY) {
            for (int i = 0; i < MAX_RAINDROPS; i++) {
                DrawLine((int)raindrops[i].pos.x, (int)raindrops[i].pos.y, 
                         (int)raindrops[i].pos.x, (int)raindrops[i].pos.y + 14, (Color){ 200, 220, 255, 180 });
            }
        }

        // Draw Platforms
        for (int i = 0; i < activePlatformCount; i++) {
            if (platformTexture.id > 0) {
                Rectangle sourceRec = { 0.0f, 0.0f, (float)platformTexture.width, (float)platformTexture.height };
                DrawTexturePro(platformTexture, sourceRec, platforms[i].rect, (Vector2){ 0, 0 }, 0.0f, WHITE);
            } else {
                DrawRectangleRec(platforms[i].rect, (Color){ 240, 130, 190, 255 });
                DrawRectangleLinesEx(platforms[i].rect, 3, (Color){ 120, 45, 150, 255 });
            }
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
                Texture2D icon = { 0 };
                if (powerups[i].type == POWERUP_DOUBLE_JUMP)  icon = texDoubleJump;
                else if (powerups[i].type == POWERUP_SUPER_JUMP)   icon = texSuperJump;
                else if (powerups[i].type == POWERUP_FEATHER_FALL) icon = texFeather;

                if (icon.id > 0) {
                    Rectangle sourceRec = { 0.0f, 0.0f, (float)icon.width, (float)icon.height };
                    Rectangle destRec   = { powerups[i].pos.x, powerups[i].pos.y, 50.0f, 50.0f };
                    Vector2 origin      = { 25.0f, 25.0f };
                    DrawTexturePro(icon, sourceRec, destRec, origin, 0.0f, WHITE);
                } else {
                    if (powerups[i].type == POWERUP_DOUBLE_JUMP) {
                        DrawPoly(powerups[i].pos, 4, 24.0f, 45.0f, SKYBLUE);
                        DrawText("2x", (int)powerups[i].pos.x - 11, (int)powerups[i].pos.y - 10, 22, DARKBLUE);
                    } 
                    else if (powerups[i].type == POWERUP_SUPER_JUMP) {
                        DrawPoly(powerups[i].pos, 5, 24.0f, 0.0f, GOLD);
                        DrawText("^", (int)powerups[i].pos.x - 7, (int)powerups[i].pos.y - 17, 30, RED);
                    } 
                    else if (powerups[i].type == POWERUP_FEATHER_FALL) {
                        DrawCircleV(powerups[i].pos, 22.0f, PINK);
                        DrawText("F", (int)powerups[i].pos.x - 7, (int)powerups[i].pos.y - 14, 26, WHITE);
                    }
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
        if (hasFeatherFallUnlocked) {
            DrawCircleLines((int)fairyPos.x, (int)fairyPos.y, fairyRadius + 5.0f, PINK);
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
        DrawText(TextFormat("Level: %d  |  Score: %d", currentLevel, score), 36, 28, 32, WHITE);
        DrawText(TextFormat("Atmosphere: %s [L to switch]", weatherName), 36, 68, 22, RAYWHITE);

        if (hasDoubleJumpUnlocked) {
            DrawText(hasUsedDoubleJump ? "[DOUBLE JUMP: USED]" : "[DOUBLE JUMP: READY]", 36, 102, 20, SKYBLUE);
        }
        if (hasSuperJumpUnlocked) {
            DrawText("[SUPER JUMP: ACTIVE]", 36, 130, 20, GOLD);
        }
        if (hasFeatherFallUnlocked) {
            DrawText("[FEATHER FALL: ACTIVE]", 36, 158, 20, PINK);
        }
    } 
    else if (currentState == STATE_GAME_OVER) {
        DrawText("THE FAIRY FELL!", screenWidth / 2 - 260, 360, 64, RED);
        DrawText(TextFormat("Reached Level %d with %d Points", currentLevel, score), screenWidth / 2 - 230, 460, 30, RAYWHITE);
        DrawText("Press [R] to Try Again", screenWidth / 2 - 190, 540, 32, YELLOW);
    }
    EndTextureMode();

    // Scale canvas into active screen window
    BeginDrawing();
    ClearBackground(BLACK);
    
    float scale = (float)GetScreenWidth() / (float)screenWidth;
    float scaleY = (float)GetScreenHeight() / (float)screenHeight;
    if (scaleY < scale) scale = scaleY;

    Rectangle srcRec = { 0.0f, 0.0f, (float)target.texture.width, -(float)target.texture.height };
    Rectangle dstRec = { 
        (GetScreenWidth() - ((float)screenWidth * scale)) * 0.5f,
        (GetScreenHeight() - ((float)screenHeight * scale)) * 0.5f,
        (float)screenWidth * scale,
        (float)screenHeight * scale 
    };

    DrawTexturePro(target.texture, srcRec, dstRec, (Vector2){ 0, 0 }, 0.0f, WHITE);
    EndDrawing();
}

// =========================================================================================
// SECTION 7: PROGRAM ENTRY POINT
// =========================================================================================

int main(void) {
    srand((unsigned int)time(NULL));

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "Fairy Sky Glade");
    SetTargetFPS(60);

    // Audio System Setup
    InitAudioDevice();

    target = LoadRenderTexture(screenWidth, screenHeight);
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);

    // Textures
    fairyTexture    = LoadTexture("fairy.png");
    platformTexture = LoadTexture("Platform.png");
    texDoubleJump   = LoadTexture("Diamond.png");
    texSuperJump    = LoadTexture("Star.png");
    texFeather      = LoadTexture("Feather.png");

    // Load Background Music Stream
   
    if (bgMusic.stream.buffer == NULL) bgMusic = LoadMusicStream("BackgroundMusic.mp3");
    

    if (bgMusic.stream.buffer != NULL) {
        musicReady = true;
        PlayMusicStream(bgMusic);
        SetMusicVolume(bgMusic, 0.65f);
    }

#if defined(PLATFORM_WEB)
    FetchWeatherData();
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    ApplyWeather(WEATHER_CLEAR);
    currentState = STATE_START;

    BeginDrawing();
    ClearBackground(BLACK);
    EndDrawing();

    while (!WindowShouldClose()) {
        UpdateDrawFrame();
    }

    UnloadRenderTexture(target);
    if (fairyTexture.id > 0)    UnloadTexture(fairyTexture);
    if (platformTexture.id > 0) UnloadTexture(platformTexture);
    if (texDoubleJump.id > 0)   UnloadTexture(texDoubleJump);
    if (texSuperJump.id > 0)    UnloadTexture(texSuperJump);
    if (texFeather.id > 0)      UnloadTexture(texFeather);

    if (musicReady) UnloadMusicStream(bgMusic);
    CloseAudioDevice();

    CloseWindow();
#endif

    return 0;
} 